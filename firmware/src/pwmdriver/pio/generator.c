/**
 * @file generator.c
 * @brief PIO generator backend implementation for the logical `pwmdriver` layer.
 *
 * This module owns the PIO-backed PWM generator used for the logical PIO channels
 * exposed by `pwmdriver`. Each backend-local channel is bound to one RP2040 PIO
 * state machine, programs a compact free-running PWM loop, and stores only the
 * realized divider, period, duty, and exported pulse-accounting state needed by
 * the higher-level driver.
 *
 * The timing model is intentionally simple: one requested frequency is mapped to a
 * quantized PIO clock divider and loop period, then the realized frequency is
 * derived back from that stored timing state for publication. Duty endpoints of
 * `0%` and `100%` are handled as static GPIO drive modes rather than by running
 * the PWM loop.
 *
 * Current limitations:
 * - Realized frequency is quantized by the available divider and loop-count space,
 *   so the applied frequency may differ slightly from the requested frequency.
 * - `pulse_count` is an elapsed-time estimate of generated periods, not a hardware-
 *   observed edge counter.
 * - The backend implements continuous generation only; finite pulse bursts are not
 *   supported by this one-state-machine design.
 */

#include "generator.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "../pwm_driver_internal.h"
#include "generator.pio.h"

/** @brief Intended upper frequency limit for the PIO generator backend. */
#define PIO_GEN_MAX_FREQ_HZ 1000000u
/** @brief Maximum quantized PIO clock divider in units of `1/256`. */
#define PIO_GEN_MAX_CLKDIV_X256 ((65535u * 256u) + 255u)

/** @brief Output mode used by one PIO generator channel. */
typedef uint8_t pio_gen_mode_t;

enum {
    PIO_GEN_MODE_DISABLED = 0, /**< Channel is stopped and driven low. */
    PIO_GEN_MODE_PWM, /**< Channel is running the PIO PWM loop. */
    PIO_GEN_MODE_STATIC_LOW, /**< Channel is configured for nonzero-frequency 0% duty. */
    PIO_GEN_MODE_STATIC_HIGH, /**< Channel is configured for nonzero-frequency 100% duty. */
};

/** @brief Runtime ownership and realized state for one PIO generator channel. */
typedef struct {
    uint64_t pulse_ref_us; /**< Reference timestamp used to accumulate estimated pulse count. */
    PIO pio; /**< Owning PIO block for the channel. */
    uint32_t clkdiv_x256; /**< Quantized PIO clock divider in units of 1/256. */
    uint32_t pulse_count; /**< Monotonic generated-period count accumulated at sync points. */
    uint32_t realized_freq_hz; /**< Cached realized output frequency in Hz used for publication and pulse estimation. */
    uint16_t period_count; /**< Current period loop count programmed into the PIO generator. */
    uint8_t sm; /**< Owning state machine index within @ref pio. */
    uint8_t duty_percent; /**< Realized PIO generator duty in percent. */
    pio_gen_mode_t mode; /**< Current generator mode for the channel. */
} pio_gen_channel_t;

/** @brief Per-channel PIO generator runtime ownership table. */
static pio_gen_channel_t pio_channels[PIO_PWM_DRIVER_COUNT];
/** @brief Tracks whether the PIO generator program is already loaded per PIO block. */
static bool pio_program_loaded[2] = {false, false};
/** @brief Cached program offsets per PIO block for the PIO generator program. */
static uint8_t pio_program_offsets[2] = {0, 0};

/**
 * @brief Map a Pico SDK PIO instance to a dense local array index.
 * @param pio Pico SDK PIO instance.
 * @return Local array index for @p pio.
 */
static uint8_t pio_index(PIO pio) {
    return pio == pio0 ? 0u : 1u;
}

/**
 * @brief Return the owning PIO block for one backend-local channel index.
 * @param channel Backend-local channel index.
 * @return Owning PIO block for @p channel.
 */
static PIO pio_for_channel(uint channel) {
    return channel < 4 ? pio0 : pio1;
}

/**
 * @brief Return the owning state machine index for one backend-local channel index.
 * @param channel Backend-local channel index.
 * @return Owning state machine index for @p channel.
 */
static uint8_t sm_for_channel(uint channel) {
    return (uint8_t)(channel % 4u);
}

/**
 * @brief Return the steady-state PIO instruction count for one PWM period.
 * @param period_count PIO loop count representing one PWM period.
 * @return Steady-state instruction count per generated PWM period.
 */
static uint32_t gen_cycles_per_period(uint16_t period_count) {
    return 3u * (uint32_t)(period_count + 1u) + 2u;
}

/**
 * @brief Derive one channel's rounded realized output frequency from one timing configuration.
 * @param period_count PIO loop count representing one PWM period.
 * @param clkdiv_x256 Quantized PIO clock divider in units of `1/256`.
 * @return Rounded realized output frequency in Hz.
 */
static uint32_t gen_realized_freq_hz_for_timing(uint16_t period_count, uint32_t clkdiv_x256) {
    uint64_t sys_clk_hz;
    uint32_t period_cycles;
    uint64_t denominator;

    if (clkdiv_x256 == 0u) {
        return 0u;
    }

    sys_clk_hz = clock_get_hz(clk_sys);
    period_cycles = gen_cycles_per_period(period_count);
    denominator = (uint64_t)clkdiv_x256 * period_cycles;
    return (uint32_t)(((sys_clk_hz * 256u) + (denominator / 2u)) / denominator);
}

/**
 * @brief Convert one duty request into the PIO loop threshold used by the generator program.
 * @param period_count Backend loop count representing one PWM period.
 * @param duty_percent Requested duty in percent.
 * @return Threshold value written into the generator state machine FIFO.
 */
static uint32_t gen_level_from_duty(uint16_t period_count, uint8_t duty_percent) {
    uint32_t level = (((uint32_t)(period_count + 1u) * (uint32_t)duty_percent) + 50u) / 100u;

    if (level > period_count + 1u) {
        level = period_count + 1u;
    }

    return level;
}

/**
 * @brief Return the current pulse count implied by one channel's cached base state.
 * @param ctx Caller-owned realized timing state.
 * @return Current pulse count derived from the cached base count and elapsed time.
 */
static uint32_t gen_pulse_count_now(const pio_gen_channel_t *ctx) {
    uint64_t elapsed_us;
    uint64_t total_pulses;

    if (ctx->realized_freq_hz == 0u) {
        return ctx->pulse_count;
    }

    elapsed_us = time_us_64() - ctx->pulse_ref_us;
    total_pulses = (uint64_t)ctx->pulse_count + (elapsed_us * (uint64_t)ctx->realized_freq_hz) / 1000000u;
    if (total_pulses > UINT32_MAX) {
        return UINT32_MAX;
    }

    return (uint32_t)total_pulses;
}

/**
 * @brief Publish the current realized state for one backend-local channel.
 * @param channel Backend-local channel index.
 */
static void gen_publish_state(uint channel) {
    pwm_driver_state_t state = {
        .freq_hz = pio_channels[channel].realized_freq_hz,
        .duty = pio_channels[channel].duty_percent,
        .pulse_count = pio_channels[channel].pulse_count,
    };

    pwm_driver_store_applied_state(PIO_PWM_CHANNEL_BASE + channel, &state);
}

/**
 * @brief Compare one direct timing candidate against the current best solution.
 * @param freq_hz Requested frequency in Hz.
 * @param sys_clk_hz System clock frequency in Hz used to derive the candidate divider.
 * @param period_count Candidate period loop count to evaluate.
 * @param best_period Caller-owned best period count updated when this candidate wins.
 * @param best_clkdiv_x256 Caller-owned best quantized divider in units of `1/256` updated when this candidate wins.
 * @param best_actual_hz Caller-owned best realized output frequency updated when this candidate wins.
 * @param best_error_hz Caller-owned best absolute frequency error updated when this candidate wins.
 */
static void gen_consider_timing_candidate(
    uint32_t freq_hz,
    uint32_t sys_clk_hz,
    uint16_t period_count,
    uint16_t *best_period,
    uint32_t *best_clkdiv_x256,
    uint32_t *best_actual_hz,
    uint32_t *best_error_hz
) {
    uint32_t cycles_per_period = gen_cycles_per_period(period_count);
    uint64_t divider_denominator = (uint64_t)freq_hz * cycles_per_period;
    uint64_t clkdiv_x256;
    uint32_t actual_hz;
    uint32_t error_hz;

    if (divider_denominator == 0u) {
        return;
    }

    clkdiv_x256 = (((uint64_t)sys_clk_hz * 256u) + (divider_denominator / 2u)) / divider_denominator;
    if (clkdiv_x256 < 256u || clkdiv_x256 > PIO_GEN_MAX_CLKDIV_X256) {
        return;
    }

    actual_hz = gen_realized_freq_hz_for_timing(period_count, (uint32_t)clkdiv_x256);
    error_hz = actual_hz > freq_hz ? (actual_hz - freq_hz) : (freq_hz - actual_hz);

    if (error_hz < *best_error_hz || (error_hz == *best_error_hz && period_count > *best_period)) {
        *best_error_hz = error_hz;
        *best_period = period_count;
        *best_clkdiv_x256 = (uint32_t)clkdiv_x256;
        *best_actual_hz = actual_hz;
    }
}

/**
 * @brief Pick one simple realizable PIO timing configuration close to the requested frequency.
 * @param freq_hz Requested frequency in Hz.
 * @param period_count_out Caller-owned destination for the selected period count.
 * @param clkdiv_x256_out Caller-owned destination for the selected quantized clock divider.
 * @return `true` when a valid timing configuration was found.
 * @note This is a small heuristic search, not an exhaustive solver. It evaluates the
 *       estimated period count and its immediate neighbors to keep the update path short.
 */
static bool gen_find_timing(uint32_t freq_hz, uint16_t *period_count_out, uint32_t *clkdiv_x256_out) {
    const uint32_t sys_clk_hz = clock_get_hz(clk_sys);

    uint32_t best_candidate_error_hz = UINT32_MAX;
    uint16_t best_candidate_period = 0;
    uint32_t best_candidate_clkdiv_x256 = 256u;
    uint32_t best_candidate_actual_hz = 0u;
    uint32_t ideal_cycles;
    uint32_t candidate_period_u32;
    uint16_t candidate_period;

    if (freq_hz == 0u) {
        return false;
    }

    ideal_cycles = (sys_clk_hz + (freq_hz / 2u)) / freq_hz;
    if (ideal_cycles < gen_cycles_per_period(1u)) {
        return false;
    }

    candidate_period_u32 = ideal_cycles <= 5u ? 1u : (ideal_cycles - 5u) / 3u;
    if (candidate_period_u32 < 1u) {
        candidate_period_u32 = 1u;
    }
    if (candidate_period_u32 > 65535u) {
        candidate_period_u32 = 65535u;
    }
    candidate_period = (uint16_t)candidate_period_u32;

    gen_consider_timing_candidate(
        freq_hz,
        sys_clk_hz,
        candidate_period,
        &best_candidate_period,
        &best_candidate_clkdiv_x256,
        &best_candidate_actual_hz,
        &best_candidate_error_hz
    );
    if (candidate_period > 1u) {
        gen_consider_timing_candidate(
            freq_hz,
            sys_clk_hz,
            (uint16_t)(candidate_period - 1u),
            &best_candidate_period,
            &best_candidate_clkdiv_x256,
            &best_candidate_actual_hz,
            &best_candidate_error_hz
        );
    }
    if (candidate_period < 65535u) {
        gen_consider_timing_candidate(
            freq_hz,
            sys_clk_hz,
            (uint16_t)(candidate_period + 1u),
            &best_candidate_period,
            &best_candidate_clkdiv_x256,
            &best_candidate_actual_hz,
            &best_candidate_error_hz
        );
    }

    if (best_candidate_actual_hz == 0u) {
        return false;
    }

    *period_count_out = best_candidate_period;
    *clkdiv_x256_out = best_candidate_clkdiv_x256;
    return true;
}

/**
 * @brief Run a small self-check over the integer timing heuristic.
 * @note This intentionally exercises one representative configuration to catch
 *       refactors that break the integer timing search or rounded-Hz reconstruction.
 */
static void gen_timing_self_check(void) {
    uint16_t period_count;
    uint32_t clkdiv_x256;
    uint32_t realized_freq_hz;

    hard_assert(gen_find_timing(1000u, &period_count, &clkdiv_x256));
    realized_freq_hz = gen_realized_freq_hz_for_timing(period_count, clkdiv_x256);
    hard_assert(realized_freq_hz > 0u);
}

/**
 * @brief Stop one PIO generator channel and drive its output to one static level.
 * @param channel Backend-local channel index.
 * @param high `true` to drive the pin high, `false` to drive it low.
 */
static void gen_drive_level(uint channel, bool high) {
    PIO pio = pio_channels[channel].pio;
    uint sm = pio_channels[channel].sm;
    uint pin = PWM_PIO_GPIO_PINS[channel];

    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, high ? 1 : 0);
}

/**
 * @brief Enable one PIO generator channel with the supplied realized timing.
 * @param channel Backend-local channel index.
 * @param period_count Backend loop count representing one PWM period.
 * @param clkdiv_x256 Quantized PIO clock divider in units of `1/256`.
 * @param duty_percent Requested duty in percent.
 */
static void gen_enable_channel(uint channel, uint16_t period_count, uint32_t clkdiv_x256, uint8_t duty_percent) {
    pio_gen_channel_t *ctx = &pio_channels[channel];
    PIO pio = ctx->pio;
    uint sm = ctx->sm;
    uint pin = PWM_PIO_GPIO_PINS[channel];
    uint16_t clkdiv_int;
    uint8_t clkdiv_frac;
    uint32_t level = gen_level_from_duty(period_count, duty_percent);

    clkdiv_int = (uint16_t)(clkdiv_x256 >> 8);
    clkdiv_frac = (uint8_t)(clkdiv_x256 & 0xffu);

    /* Static modes hand the pin to SIO, so only non-PWM -> PWM transitions must rebind it to PIO ownership. */
    if (ctx->mode != PIO_GEN_MODE_PWM) {
        pio_gpio_init(pio, pin);
        pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    }

    pio_sm_set_clkdiv_int_frac(pio, sm, clkdiv_int, clkdiv_frac);
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);

    pio_sm_put_blocking(pio, sm, level);
    pio_sm_put_blocking(pio, sm, period_count);

    pio_sm_set_enabled(pio, sm, true);
}

/** @copydoc pio_gen_init */
void pio_gen_init(void) {
    if (!pio_program_loaded[0]) {
        pio_program_offsets[0] = pio_add_program(pio0, &generator_program);
        pio_program_loaded[0] = true;
    }

    if (!pio_program_loaded[1]) {
        pio_program_offsets[1] = pio_add_program(pio1, &generator_program);
        pio_program_loaded[1] = true;
    }

    gen_timing_self_check();

    for (int i = 0; i < PIO_PWM_DRIVER_COUNT; i++) {
        pio_channels[i].pio = pio_for_channel(i);
        pio_channels[i].sm = sm_for_channel(i);
        pio_channels[i].mode = PIO_GEN_MODE_DISABLED;
        pio_channels[i].period_count = 0;
        pio_channels[i].clkdiv_x256 = 256u;
        pio_channels[i].realized_freq_hz = 0u;
        pio_channels[i].duty_percent = 50u;
        pio_channels[i].pulse_count = 0;
        pio_channels[i].pulse_ref_us = time_us_64();

        generator_program_init(
            pio_channels[i].pio,
            pio_channels[i].sm,
            pio_program_offsets[pio_index(pio_channels[i].pio)],
            PWM_PIO_GPIO_PINS[i]
        );
        gen_drive_level(i, false);
    }
}

/** @copydoc pio_gen_set_freq */
bool pio_gen_set_freq(uint channel, uint32_t freq_hz, uint8_t duty) {
    uint16_t period_count;
    uint8_t duty_percent;
    pio_gen_channel_t *ctx;

    if (channel >= PIO_PWM_DRIVER_COUNT) return false;

    ctx = &pio_channels[channel];

    ctx->pulse_count = gen_pulse_count_now(ctx);
    ctx->pulse_ref_us = time_us_64();

    duty_percent = duty > 100u ? 100u : duty;

    if (freq_hz == 0u) {
        bool high = duty_percent == 100u;

        gen_drive_level(channel, high);
        ctx->mode = high ? PIO_GEN_MODE_STATIC_HIGH : PIO_GEN_MODE_STATIC_LOW;
        ctx->duty_percent = duty_percent;
        ctx->period_count = 0;
        ctx->clkdiv_x256 = 0u;
        ctx->realized_freq_hz = 0u;
        gen_publish_state(channel);
        return true;
    }

    if (freq_hz > PIO_GEN_MAX_FREQ_HZ) {
        return false;
    }

    period_count = 0u;
    uint32_t clkdiv_x256 = 256u;
    if (!gen_find_timing(freq_hz, &period_count, &clkdiv_x256)) {
        return false;
    }

    ctx->duty_percent = duty_percent;
    ctx->period_count = period_count;
    ctx->clkdiv_x256 = clkdiv_x256;
    ctx->realized_freq_hz = gen_realized_freq_hz_for_timing(period_count, clkdiv_x256);

    if (duty_percent == 0u || duty_percent == 100u) {
        ctx->mode = duty_percent == 0u ? PIO_GEN_MODE_STATIC_LOW : PIO_GEN_MODE_STATIC_HIGH;
        ctx->realized_freq_hz = 0u;
        gen_drive_level(channel, duty_percent == 100u);
    } else {
        gen_enable_channel(channel, period_count, clkdiv_x256, duty_percent);
        ctx->mode = PIO_GEN_MODE_PWM;
    }

    gen_publish_state(channel);
    return true;
}

/** @copydoc pio_gen_get */
bool pio_gen_get(uint channel, pwm_driver_state_t *state) {
    if (channel >= PIO_PWM_DRIVER_COUNT || state == NULL) return false;
    state->freq_hz = pio_channels[channel].realized_freq_hz;
    state->duty = pio_channels[channel].duty_percent;
    /* Shared pwm_driver_get() owns live PIO pulse extrapolation; this backend getter returns the cached base count. */
    state->pulse_count = pio_channels[channel].pulse_count;
    return true;
}