/**
 * @file generator.c
 * @brief PIO PWM generator backend implementation for the logical `pwmdriver` layer.
 */

#include "generator.h"

#include "../pwm_driver.h"
#include "../pwm_driver_internal.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"

#include "generator.pio.h"

/** @brief Realized PIO PWM frequencies in backend-local channel order. */
static float requested_freqs[PIO_PWM_DRIVER_COUNT] = {0};
/** @brief Realized PIO PWM duties in backend-local channel order. */
static float duties[PIO_PWM_DRIVER_COUNT] = {0};
/** @brief Enabled-state cache for PIO PWM backend channels. */
static bool enabled[PIO_PWM_DRIVER_COUNT] = {false};
/** @brief Monotonic pulse counters updated from the PIO interrupt path. */
static volatile uint32_t pulse_counts[PIO_PWM_DRIVER_COUNT] = {0};

/** @brief Runtime ownership and timing state for one PIO PWM backend channel. */
typedef struct {
    PIO pio; /**< Owning PIO block for the channel. */
    uint sm; /**< Owning state machine index within @ref pio. */
    uint program_offset; /**< Loaded program offset used by the state machine. */
    uint32_t period_count; /**< Current period loop count programmed into the PIO backend. */
    float clkdiv; /**< Quantized PIO clock divider used for the realized frequency. */
} pio_pwm_channel_ctx_t;

/** @brief Per-channel PIO runtime ownership table. */
static pio_pwm_channel_ctx_t pio_pwm_channels[PIO_PWM_DRIVER_COUNT];
/** @brief Tracks whether the PIO PWM program is already loaded per PIO block. */
static bool pio_program_loaded[2] = {false, false};
/** @brief Cached program offsets per PIO block for the PIO PWM program. */
static uint pio_program_offsets[2] = {0, 0};

/** @brief Map a Pico SDK PIO instance to a dense local array index. */
static uint pio_index(PIO pio) {
    return pio == pio0 ? 0u : 1u;
}

/** @brief Return the owning PIO block for one backend-local channel index. */
static PIO pio_for_channel(uint channel) {
    return channel < 4 ? pio0 : pio1;
}

/** @brief Return the owning state machine index for one backend-local channel index. */
static uint sm_for_channel(uint channel) {
    return channel % 4u;
}

/** @brief Shared IRQ worker that advances pulse counters for one PIO block. */
static void pio_pwm_generator_irq_handler(PIO pio) {
    uint pio_id = pio_index(pio);

    for (uint channel = 0; channel < PIO_PWM_DRIVER_COUNT; channel++) {
        if (pio_index(pio_pwm_channels[channel].pio) != pio_id) {
            continue;
        }

        uint irq_index = pio_pwm_channels[channel].sm;
        if (pio_interrupt_get(pio, irq_index)) {
            pulse_counts[channel]++;
            pwm_driver_store_pulse_count(PIO_PWM_CHANNEL_BASE + channel, pulse_counts[channel]);
            pio_interrupt_clear(pio, irq_index);
        }
    }
}

/** @brief IRQ wrapper for `pio0`. */
static void pio0_pwm_generator_irq_handler(void) {
    pio_pwm_generator_irq_handler(pio0);
}

/** @brief IRQ wrapper for `pio1`. */
static void pio1_pwm_generator_irq_handler(void) {
    pio_pwm_generator_irq_handler(pio1);
}

/**
 * @brief Program a new PIO PWM period value into one state machine.
 * @param pio Owning PIO block.
 * @param sm Owning state machine index.
 * @param period_count Backend loop count representing one PWM period.
 */
static void pio_pwm_generator_set_period(PIO pio, uint sm, uint32_t period_count) {
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_put_blocking(pio, sm, period_count);
    pio_sm_exec(pio, sm, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm, pio_encode_out(pio_isr, 32));
}

/**
 * @brief Program a new duty level value into one PIO PWM state machine.
 * @param pio Owning PIO block.
 * @param sm Owning state machine index.
 * @param level Backend level count representing the active duty window.
 */
static void pio_pwm_generator_set_level(PIO pio, uint sm, uint32_t level) {
    pio_sm_put_blocking(pio, sm, level);
}

/**
 * @brief Search for a realizable PIO timing configuration close to the requested frequency.
 * @param freq_hz Requested frequency in Hz.
 * @param period_count_out Caller-owned destination for the selected period count.
 * @param clkdiv_out Caller-owned destination for the selected quantized clock divider.
 * @param actual_freq_out Caller-owned destination for the realized frequency.
 * @return `true` when a valid timing configuration was found.
 */
static bool pio_pwm_generator_find_timing(float freq_hz, uint32_t *period_count_out, float *clkdiv_out, float *actual_freq_out) {
    const float sys_clk_hz = (float)clock_get_hz(clk_sys);
    const float max_clkdiv = 65535.0f + 255.0f / 256.0f;

    float best_err = 1e30f;
    uint32_t best_period = 0;
    float best_clkdiv = 1.0f;
    float best_actual = 0.0f;

    for (uint32_t period_count = 1; period_count <= 65535; period_count++) {
        float cycles_per_period = (float)(3u * period_count + 7u);
        float clkdiv = sys_clk_hz / (freq_hz * cycles_per_period);
        if (clkdiv < 1.0f || clkdiv > max_clkdiv) {
            continue;
        }

        float quantized_clkdiv = (float)((uint32_t)(clkdiv * 256.0f + 0.5f)) / 256.0f;
        if (quantized_clkdiv < 1.0f) quantized_clkdiv = 1.0f;
        if (quantized_clkdiv > max_clkdiv) quantized_clkdiv = max_clkdiv;

        float actual_freq = sys_clk_hz / (quantized_clkdiv * cycles_per_period);
        float err = actual_freq > freq_hz ? (actual_freq - freq_hz) / freq_hz : (freq_hz - actual_freq) / freq_hz;
        if (err < best_err) {
            best_err = err;
            best_period = period_count;
            best_clkdiv = quantized_clkdiv;
            best_actual = actual_freq;

            if (err < 1e-6f) {
                break;
            }
        }
    }

    if (best_actual <= 0.0f) {
        return false;
    }

    *period_count_out = best_period;
    *clkdiv_out = best_clkdiv;
    *actual_freq_out = best_actual;
    return true;
}

/** @brief Disable one PIO PWM backend channel and drive its output low. */
static void pio_pwm_generator_disable_channel(uint channel) {
    PIO pio = pio_pwm_channels[channel].pio;
    uint sm = pio_pwm_channels[channel].sm;
    uint pin = PWM_PIO_GPIO_PINS[channel];

    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_interrupt_clear(pio, sm);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
}

/**
 * @brief Enable one PIO PWM backend channel with the supplied realized timing.
 * @param channel Backend-local channel index.
 * @param period_count Backend loop count representing one PWM period.
 * @param clkdiv Quantized PIO clock divider.
 * @param duty Requested duty in the normalized range `[0.0, 1.0]`.
 */
static void pio_pwm_generator_enable_channel(uint channel, uint32_t period_count, float clkdiv, float duty) {
    PIO pio = pio_pwm_channels[channel].pio;
    uint sm = pio_pwm_channels[channel].sm;
    uint pin = PWM_PIO_GPIO_PINS[channel];
    uint offset = pio_pwm_channels[channel].program_offset;

    uint32_t level = (uint32_t)((float)(period_count + 1u) * duty + 0.5f);
    if (level > period_count + 1u) {
        level = period_count + 1u;
    }

    generator_program_init(pio, sm, offset, pin);
    pio_sm_set_clkdiv(pio, sm, clkdiv);
    pio_pwm_generator_set_period(pio, sm, period_count);
    pio_pwm_generator_set_level(pio, sm, level);
    pio_interrupt_clear(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

/** @copydoc pio_pwm_generator_init */
void pio_pwm_generator_init(void) {
    for (uint pio_id = 0; pio_id < 2; pio_id++) {
        PIO pio = pio_id == 0 ? pio0 : pio1;
        if (!pio_program_loaded[pio_id]) {
            pio_program_offsets[pio_id] = pio_add_program(pio, &generator_program);
            pio_program_loaded[pio_id] = true;
        }
    }

    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_pwm_generator_irq_handler);
    irq_set_enabled(PIO0_IRQ_0, true);
    irq_set_exclusive_handler(PIO1_IRQ_0, pio1_pwm_generator_irq_handler);
    irq_set_enabled(PIO1_IRQ_0, true);

    for (int i = 0; i < PIO_PWM_DRIVER_COUNT; i++) {
        pio_pwm_channels[i].pio = pio_for_channel(i);
        pio_pwm_channels[i].sm = sm_for_channel(i);
        pio_pwm_channels[i].program_offset = pio_program_offsets[pio_index(pio_pwm_channels[i].pio)];
        pio_pwm_channels[i].period_count = 0;
        pio_pwm_channels[i].clkdiv = 1.0f;

        pio_set_irq0_source_enabled(
            pio_pwm_channels[i].pio,
            (enum pio_interrupt_source)(pis_interrupt0 + pio_pwm_channels[i].sm),
            true
        );

        gpio_init(PWM_PIO_GPIO_PINS[i]);
        gpio_set_dir(PWM_PIO_GPIO_PINS[i], GPIO_OUT);
        gpio_put(PWM_PIO_GPIO_PINS[i], 0);

        requested_freqs[i] = 0.0f;
        duties[i] = 0.5f;
        enabled[i] = false;
        pulse_counts[i] = 0;
    }
}

/** @copydoc pio_pwm_generator_set_freq */
bool pio_pwm_generator_set_freq(uint channel, float freq_hz, float duty) {
    pwm_driver_state_t state;

    if (channel >= PIO_PWM_DRIVER_COUNT) return false;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    if (freq_hz < 0.0f) return false;

    duties[channel] = duty;

    if (freq_hz <= 0.0f) {
        pio_pwm_generator_disable_channel(channel);
        requested_freqs[channel] = 0.0f;
        enabled[channel] = false;
        pio_pwm_channels[channel].period_count = 0;
        state.freq_hz = pwm_driver_freq_hz_from_float(requested_freqs[channel]);
        state.duty = pwm_driver_duty_percent_from_float(duties[channel]);
        state.pulse_count = pulse_counts[channel];
        pwm_driver_store_applied_state(PIO_PWM_CHANNEL_BASE + channel, &state);
        return true;
    }

    uint32_t period_count = 0;
    float clkdiv = 1.0f;
    float actual_freq = 0.0f;
    if (!pio_pwm_generator_find_timing(freq_hz, &period_count, &clkdiv, &actual_freq)) {
        return false;
    }

    pio_pwm_generator_enable_channel(channel, period_count, clkdiv, duty);

    requested_freqs[channel] = actual_freq;
    enabled[channel] = true;
    pio_pwm_channels[channel].period_count = period_count;
    pio_pwm_channels[channel].clkdiv = clkdiv;
    state.freq_hz = pwm_driver_freq_hz_from_float(requested_freqs[channel]);
    state.duty = pwm_driver_duty_percent_from_float(duties[channel]);
    state.pulse_count = pulse_counts[channel];
    pwm_driver_store_applied_state(PIO_PWM_CHANNEL_BASE + channel, &state);
    return true;
}

/** @copydoc pio_pwm_generator_get */
bool pio_pwm_generator_get(uint channel, pwm_driver_state_t *state) {
    if (channel >= PIO_PWM_DRIVER_COUNT || state == NULL) return false;
    state->freq_hz = pwm_driver_freq_hz_from_float(requested_freqs[channel]);
    state->duty = pwm_driver_duty_percent_from_float(duties[channel]);
    state->pulse_count = pulse_counts[channel];
    return true;
}