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

#include <math.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "../pwm_driver_internal.h"
#include "generator.pio.h"

/** @brief Intended upper frequency limit for the PIO generator backend. */
#define PIO_GEN_MAX_FREQ_HZ 1000000u

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
    uint16_t period_count; /**< Current period loop count programmed into the PIO generator. */
    uint8_t sm; /**< Owning state machine index within @ref pio. */
    uint8_t program_offset; /**< Loaded program offset used by the state machine. */
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
 * @brief Return the steady-state PIO instruction cost for one PWM period.
 * @param period_count PIO loop count representing one PWM period.
 * @return Steady-state instruction cost per generated PWM period.
 */
static double gen_cycles_per_period(uint16_t period_count) {
    return 3.0 * (double)(period_count + 1u) + 2.0;
}

/**
 * @brief Derive one channel's realized output frequency from its stored timing state.
 * @param ctx Caller-owned realized timing state.
 * @return Exact realized output frequency in Hz, or `0.0` when the channel is disabled.
 */
static double gen_realized_freq_exact_hz(const pio_gen_channel_t *ctx) {
    double sys_clk_hz;
    double cycles_per_period;

    if (ctx->mode == PIO_GEN_MODE_DISABLED || ctx->clkdiv_x256 == 0u) {
        return 0.0;
    }

    sys_clk_hz = (double)clock_get_hz(clk_sys);
    cycles_per_period = gen_cycles_per_period(ctx->period_count);
    return (sys_clk_hz * 256.0) / ((double)ctx->clkdiv_x256 * cycles_per_period);
}

/**
 * @brief Derive one channel's rounded realized output frequency from its stored timing state.
 * @param ctx Caller-owned realized timing state.
 * @return Rounded realized output frequency in Hz.
 */
static uint32_t gen_realized_freq_hz(const pio_gen_channel_t *ctx) {
    return (uint32_t)(gen_realized_freq_exact_hz(ctx) + 0.5);
}

/**
 * @brief Publish the current realized state for one backend-local channel.
 * @param channel Backend-local channel index.
 */
static void gen_publish_state(uint channel) {
    pwm_driver_state_t state = {
        .freq_hz = gen_realized_freq_hz(&pio_channels[channel]),
        .duty = pio_channels[channel].duty_percent,
        .pulse_count = pio_channels[channel].pulse_count,
    };

    pwm_driver_store_applied_state(PIO_PWM_CHANNEL_BASE + channel, &state);
}

/**
 * @brief Accumulate elapsed generated pulses into the local monotonic counter.
 * @param channel Backend-local channel index.
 */
static void gen_sync_pulses(uint channel) {
    pio_gen_channel_t *ctx = &pio_channels[channel];
    double realized_freq_hz;
    uint64_t now_us;
    uint64_t elapsed_us;
    uint64_t delta_pulses;
    double consumed_us;

    realized_freq_hz = gen_realized_freq_exact_hz(ctx);
    if (realized_freq_hz <= 0.0) {
        return;
    }

    now_us = time_us_64();
    elapsed_us = now_us - ctx->pulse_ref_us;
    if (elapsed_us == 0u) {
        return;
    }

    delta_pulses = (uint64_t)(((double)elapsed_us * realized_freq_hz) / 1000000.0);
    if (delta_pulses == 0u) {
        return;
    }

    if (delta_pulses > (uint64_t)(UINT32_MAX - ctx->pulse_count)) {
        ctx->pulse_count = UINT32_MAX;
        ctx->pulse_ref_us = now_us;
        return;
    }

    ctx->pulse_count += (uint32_t)delta_pulses;
    consumed_us = ((double)delta_pulses * 1000000.0) / realized_freq_hz;
    ctx->pulse_ref_us += (uint64_t)consumed_us;
}

/**
 * @brief Clamp one unsigned value into a closed interval.
 * @param value Input value to clamp.
 * @param min_value Inclusive lower bound.
 * @param max_value Inclusive upper bound.
 * @return Clamped value in the interval [`min_value`, `max_value`].
 */
static uint32_t gen_clamp_u32(uint32_t value, uint32_t min_value, uint32_t max_value) {
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

/**
 * @brief Compare one direct timing candidate against the current best solution.
 * @param freq_hz Requested frequency in Hz.
 * @param sys_clk_hz System clock frequency in Hz used to derive the candidate divider.
 * @param max_clkdiv Maximum realizable PIO clock divider.
 * @param err_epsilon Tolerance used when comparing nearly equal relative errors.
 * @param period_count Candidate period loop count to evaluate.
 * @param best_period Caller-owned best period count updated when this candidate wins.
 * @param best_clkdiv_x256 Caller-owned best quantized divider in units of `1/256` updated when this candidate wins.
 * @param best_actual Caller-owned best realized output frequency updated when this candidate wins.
 * @param best_err Caller-owned best relative frequency error updated when this candidate wins.
 */
static void gen_consider_timing(
    uint32_t freq_hz,
    double sys_clk_hz,
    double max_clkdiv,
    double err_epsilon,
    uint16_t period_count,
    uint16_t *best_period,
    uint32_t *best_clkdiv_x256,
    double *best_actual,
    double *best_err
) {
    double cycles_per_period = gen_cycles_per_period(period_count);
    double clkdiv = sys_clk_hz / ((double)freq_hz * cycles_per_period);

    if (clkdiv < 1.0 || clkdiv > max_clkdiv) {
        return;
    }

    uint32_t quantized_clkdiv_x256 = (uint32_t)(clkdiv * 256.0 + 0.5);
    if (quantized_clkdiv_x256 < 256u) quantized_clkdiv_x256 = 256u;
    if ((double)quantized_clkdiv_x256 / 256.0 > max_clkdiv) quantized_clkdiv_x256 = (uint32_t)(max_clkdiv * 256.0);

    double actual_freq = (sys_clk_hz * 256.0) / ((double)quantized_clkdiv_x256 * cycles_per_period);
    double err = actual_freq > (double)freq_hz
        ? (actual_freq - (double)freq_hz) / (double)freq_hz
        : ((double)freq_hz - actual_freq) / (double)freq_hz;

    if (err < *best_err - err_epsilon || (err <= *best_err + err_epsilon && period_count > *best_period)) {
        *best_err = err;
        *best_period = period_count;
        *best_clkdiv_x256 = quantized_clkdiv_x256;
        *best_actual = actual_freq;
    }
}

/**
 * @brief Pick one simple realizable PIO timing configuration close to the requested frequency.
 * @param freq_hz Requested frequency in Hz.
 * @param period_count_out Caller-owned destination for the selected period count.
 * @param clkdiv_x256_out Caller-owned destination for the selected quantized clock divider.
 * @return `true` when a valid timing configuration was found.
 */
static bool gen_find_timing(uint32_t freq_hz, uint16_t *period_count_out, uint32_t *clkdiv_x256_out) {
    const double sys_clk_hz = (double)clock_get_hz(clk_sys);
    const double max_clkdiv = 65535.0 + 255.0 / 256.0;
    const double err_epsilon = 1e-12;

    double best_err = 1e30;
    uint16_t best_period = 0;
    uint32_t best_clkdiv_x256 = 256u;
    double best_actual = 0.0;
    double ideal_cycles = sys_clk_hz / (double)freq_hz;
    uint32_t candidate_period_u32;
    uint16_t candidate_period;

    if (ideal_cycles < gen_cycles_per_period(1u)) {
        return false;
    }

    candidate_period_u32 = ideal_cycles <= 5.0 ? 1u : (uint32_t)((ideal_cycles - 5.0) / 3.0);
    candidate_period_u32 = gen_clamp_u32(candidate_period_u32, 1u, 65535u);
    candidate_period = (uint16_t)candidate_period_u32;

    gen_consider_timing(freq_hz, sys_clk_hz, max_clkdiv, err_epsilon, candidate_period, &best_period, &best_clkdiv_x256, &best_actual, &best_err);
    if (candidate_period > 1u) {
        gen_consider_timing(freq_hz, sys_clk_hz, max_clkdiv, err_epsilon, (uint16_t)(candidate_period - 1u), &best_period, &best_clkdiv_x256, &best_actual, &best_err);
    }
    if (candidate_period < 65535u) {
        gen_consider_timing(freq_hz, sys_clk_hz, max_clkdiv, err_epsilon, (uint16_t)(candidate_period + 1u), &best_period, &best_clkdiv_x256, &best_actual, &best_err);
    }

    if (best_actual <= 0.0) {
        return false;
    }

    *period_count_out = best_period;
    *clkdiv_x256_out = best_clkdiv_x256;
    return true;
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
 * @brief Disable one PIO generator channel and drive its output low.
 * @param channel Backend-local channel index.
 */
static void gen_disable_channel(uint channel) {
    gen_drive_level(channel, false);
}

/**
 * @brief Enable one PIO generator channel with the supplied realized timing.
 * @param channel Backend-local channel index.
 * @param period_count Backend loop count representing one PWM period.
 * @param clkdiv_x256 Quantized PIO clock divider in units of `1/256`.
 * @param duty_percent Requested duty in percent.
 */
static void gen_enable_channel(uint channel, uint32_t period_count, uint32_t clkdiv_x256, uint8_t duty_percent) {
    pio_gen_channel_t *ctx = &pio_channels[channel];
    PIO pio = ctx->pio;
    uint sm = ctx->sm;
    uint pin = PWM_PIO_GPIO_PINS[channel];
    uint offset = ctx->program_offset;

    uint32_t level = (((period_count + 1u) * (uint32_t)duty_percent) + 50u) / 100u;
    if (level > period_count + 1u) {
        level = period_count + 1u;
    }

    generator_program_init(pio, sm, offset, pin);
    pio_sm_set_clkdiv(pio, sm, (float)clkdiv_x256 / 256.0f);
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

    for (int i = 0; i < PIO_PWM_DRIVER_COUNT; i++) {
        pio_channels[i].pio = pio_for_channel(i);
        pio_channels[i].sm = sm_for_channel(i);
        pio_channels[i].program_offset = pio_program_offsets[pio_index(pio_channels[i].pio)];
        pio_channels[i].mode = PIO_GEN_MODE_DISABLED;
        pio_channels[i].period_count = 0;
        pio_channels[i].clkdiv_x256 = 256u;
        pio_channels[i].duty_percent = 50u;
        pio_channels[i].pulse_count = 0;
        pio_channels[i].pulse_ref_us = time_us_64();

        gpio_init(PWM_PIO_GPIO_PINS[i]);
        gpio_set_dir(PWM_PIO_GPIO_PINS[i], GPIO_OUT);
        gpio_put(PWM_PIO_GPIO_PINS[i], 0);
    }
}

/** @copydoc pio_gen_set_freq */
bool pio_gen_set_freq(uint channel, uint32_t freq_hz, uint8_t duty) {
    uint16_t period_count;
    uint8_t duty_percent;
    pio_gen_channel_t *ctx;

    if (channel >= PIO_PWM_DRIVER_COUNT) return false;

    ctx = &pio_channels[channel];

    gen_sync_pulses(channel);

    duty_percent = duty > 100u ? 100u : duty;

    if (freq_hz == 0u) {
        bool high = duty_percent == 100u;

        gen_drive_level(channel, high);
        ctx->mode = high ? PIO_GEN_MODE_STATIC_HIGH : PIO_GEN_MODE_STATIC_LOW;
        ctx->duty_percent = duty_percent;
        ctx->period_count = 0;
        ctx->clkdiv_x256 = 0u;
        ctx->pulse_ref_us = time_us_64();
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

    if (duty_percent == 0u || duty_percent == 100u) {
        ctx->mode = duty_percent == 0u ? PIO_GEN_MODE_STATIC_LOW : PIO_GEN_MODE_STATIC_HIGH;
        gen_drive_level(channel, duty_percent == 100u);
    } else {
        ctx->mode = PIO_GEN_MODE_PWM;
        gen_enable_channel(channel, period_count, clkdiv_x256, duty_percent);
    }

    ctx->pulse_ref_us = time_us_64();
    gen_publish_state(channel);
    return true;
}

/** @copydoc pio_gen_get */
bool pio_gen_get(uint channel, pwm_driver_state_t *state) {
    if (channel >= PIO_PWM_DRIVER_COUNT || state == NULL) return false;
    gen_sync_pulses(channel);
    state->freq_hz = gen_realized_freq_hz(&pio_channels[channel]);
    state->duty = pio_channels[channel].duty_percent;
    state->pulse_count = pio_channels[channel].pulse_count;
    return true;
}