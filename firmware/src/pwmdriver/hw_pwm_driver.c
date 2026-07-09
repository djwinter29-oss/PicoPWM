#include "hw_pwm_driver.h"

#include "pwm_driver.h"
#include "pwm_driver_internal.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"

static float actual_freqs[HW_PWM_COUNT] = {0};
static float duties[HW_PWM_COUNT] = {0};
static bool enabled[HW_PWM_COUNT] = {false};
static uint16_t wraps[HW_PWM_COUNT] = {0};
static volatile uint32_t pulse_counts[HW_PWM_COUNT] = {0};

static uint32_t hw_pwm_level_from_duty(uint32_t top, float duty) {
    if (duty <= 0.0f) {
        return 0;
    }
    if (duty >= 1.0f) {
        return top + 1u;
    }

    uint32_t level = (uint32_t)((float)(top + 1u) * duty + 0.5f);
    if (level > top + 1u) {
        level = top + 1u;
    }
    return level;
}

static float fabsf_local(float x) {
    return x < 0.0f ? -x : x;
}

static void hw_pwm_irq_handler(void) {
    uint32_t status = pwm_get_irq_status_mask();
    for (int i = 0; i < HW_PWM_COUNT; i++) {
        uint slice = pwm_gpio_to_slice_num(PWM_HW_GPIO_PINS[i]);
        if (status & (1u << slice)) {
            pulse_counts[i]++;
            pwm_driver_store_pulse_count(HW_PWM_CHANNEL_BASE + i, pulse_counts[i]);
            pwm_clear_irq(slice);
        }
    }
}

void hw_pwm_driver_init(void) {
    for (int i = 0; i < HW_PWM_COUNT; i++) {
        uint gpio = PWM_HW_GPIO_PINS[i];
        gpio_set_function(gpio, GPIO_FUNC_PWM);

        uint slice = pwm_gpio_to_slice_num(gpio);
        uint ch = pwm_gpio_to_channel(gpio);

        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv(&cfg, 1.0f);
        pwm_config_set_wrap(&cfg, 255);
        pwm_init(slice, &cfg, true);

        pwm_set_chan_level(slice, ch, 0);
        pwm_set_enabled(slice, false);

        actual_freqs[i] = 0.0f;
        duties[i] = 0.5f;
        enabled[i] = false;
        wraps[i] = 255;
        pulse_counts[i] = 0;
    }

    for (int i = 0; i < HW_PWM_COUNT; i++) {
        uint slice = pwm_gpio_to_slice_num(PWM_HW_GPIO_PINS[i]);
        pwm_clear_irq(slice);
        pwm_set_irq_enabled(slice, true);
    }

    irq_set_exclusive_handler(PWM_IRQ_WRAP, hw_pwm_irq_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

bool hw_pwm_driver_set_freq(uint channel, float freq_hz, float duty) {
    pwm_driver_state_t state;

    if (channel >= HW_PWM_COUNT) return false;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    uint gpio = PWM_HW_GPIO_PINS[channel];
    uint slice = pwm_gpio_to_slice_num(gpio);
    uint ch = pwm_gpio_to_channel(gpio);

    duties[channel] = duty;

    if (freq_hz <= 0.0f) {
        pwm_set_enabled(slice, false);
        pwm_set_chan_level(slice, ch, 0);
        enabled[channel] = false;
        actual_freqs[channel] = 0.0f;
        state.freq_hz = actual_freqs[channel];
        state.duty = duties[channel];
        state.pulse_count = pulse_counts[channel];
        pwm_driver_store_applied_state(HW_PWM_CHANNEL_BASE + channel, &state);
        return true;
    }

    uint32_t sys_clk = clock_get_hz(clk_sys);

    uint32_t best_top = 255;
    float best_div = 1.0f;
    float best_err = 1e10f;

    for (uint32_t int_div = 1; int_div <= 255; int_div++) {
        for (uint32_t frac_div = 0; frac_div < 16; frac_div++) {
            float div = (float)int_div + (float)frac_div / 16.0f;

            float top_f = ((float)sys_clk / (freq_hz * div)) - 1.0f;
            if (top_f < 0.0f) continue;
            if (top_f > 65535.0f) continue;

            uint32_t top = (uint32_t)(top_f + 0.5f);
            if (top < 1) top = 1;
            if (top > 65535) continue;

            float actual_freq = (float)sys_clk / (div * (top + 1));
            float err = fabsf_local(actual_freq - freq_hz) / freq_hz;

            if (err < best_err) {
                best_err = err;
                best_top = top;
                best_div = div;

                if (err < 1e-6f) {
                    int_div = 256;
                    break;
                }
            }
        }
    }

    pwm_set_enabled(slice, false);
    pwm_set_wrap(slice, best_top);
    pwm_set_clkdiv(slice, best_div);
    pwm_set_chan_level(slice, ch, hw_pwm_level_from_duty(best_top, duty));
    pwm_set_enabled(slice, true);

    enabled[channel] = true;
    actual_freqs[channel] = (float)sys_clk / (best_div * (best_top + 1));
    wraps[channel] = (uint16_t)best_top;

    state.freq_hz = actual_freqs[channel];
    state.duty = duties[channel];
    state.pulse_count = pulse_counts[channel];
    pwm_driver_store_applied_state(HW_PWM_CHANNEL_BASE + channel, &state);

    return true;
}

bool hw_pwm_driver_get(uint channel, pwm_driver_state_t *state) {
    if (channel >= HW_PWM_COUNT || state == NULL) return false;
    state->freq_hz = actual_freqs[channel];
    state->duty = duties[channel];
    state->pulse_count = pulse_counts[channel];
    return true;
}