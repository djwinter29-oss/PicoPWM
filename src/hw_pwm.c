#include "hw_pwm.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

// GPIO pins for 8 hardware PWM channels.
// Each pin uses a different PWM slice, channel A, so all 8 can have independent frequencies.
// Mapping:
//   GPIO0  -> PWM slice 0, channel A
//   GPIO2  -> PWM slice 1, channel A
//   GPIO4  -> PWM slice 2, channel A
//   GPIO6  -> PWM slice 3, channel A
//   GPIO8  -> PWM slice 4, channel A
//   GPIO10 -> PWM slice 5, channel A
//   GPIO12 -> PWM slice 6, channel A
//   GPIO14 -> PWM slice 7, channel A
static const uint hw_pwm_gpios[HW_PWM_COUNT] = {
    0, 2, 4, 6, 8, 10, 12, 14
};

static float actual_freqs[HW_PWM_COUNT] = {0};
static float duties[HW_PWM_COUNT] = {0};
static bool enabled[HW_PWM_COUNT] = {false};
static uint16_t wraps[HW_PWM_COUNT] = {0};
static volatile uint32_t pulse_counts[HW_PWM_COUNT] = {0};

static float fabsf_local(float x) {
    return x < 0.0f ? -x : x;
}

static void hw_pwm_irq_handler(void) {
    uint32_t status = pwm_get_irq_status_mask();
    for (int i = 0; i < HW_PWM_COUNT; i++) {
        uint slice = pwm_gpio_to_slice_num(hw_pwm_gpios[i]);
        if (status & (1u << slice)) {
            pulse_counts[i]++;
            pwm_clear_irq(slice);
        }
    }
}

void hw_pwm_init(void) {
    for (int i = 0; i < HW_PWM_COUNT; i++) {
        uint gpio = hw_pwm_gpios[i];
        gpio_set_function(gpio, GPIO_FUNC_PWM);

        uint slice = pwm_gpio_to_slice_num(gpio);
        uint ch = pwm_gpio_to_channel(gpio);

        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv(&cfg, 1.0f);
        pwm_config_set_wrap(&cfg, 255);
        pwm_init(slice, &cfg, true);

        // Disable output initially (freq = 0 at boot).
        pwm_set_chan_level(slice, ch, 0);
        pwm_set_enabled(slice, false);

        actual_freqs[i] = 0.0f;
        duties[i] = 0.5f;
        enabled[i] = false;
        wraps[i] = 255;
        pulse_counts[i] = 0;
    }

    // Enable PWM wrap interrupts for pulse counting.
    for (int i = 0; i < HW_PWM_COUNT; i++) {
        uint slice = pwm_gpio_to_slice_num(hw_pwm_gpios[i]);
        pwm_clear_irq(slice);
        pwm_set_irq_enabled(slice, true);
    }
    irq_set_exclusive_handler(PWM_IRQ_WRAP, hw_pwm_irq_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);
}

bool hw_pwm_set_freq(uint channel, float freq_hz, float duty) {
    if (channel >= HW_PWM_COUNT) return false;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    uint gpio = hw_pwm_gpios[channel];
    uint slice = pwm_gpio_to_slice_num(gpio);
    uint ch = pwm_gpio_to_channel(gpio);

    duties[channel] = duty;

    if (freq_hz <= 0.0f) {
        // freq = 0 means disable.
        pwm_set_enabled(slice, false);
        pwm_set_chan_level(slice, ch, 0);
        enabled[channel] = false;
        actual_freqs[channel] = 0.0f;
        return true;
    }

    uint32_t sys_clk = clock_get_hz(clk_sys);
    const float max_div = 255.0f + 15.0f / 16.0f;

    uint32_t best_top = 255;
    float best_div = 1.0f;
    float best_err = 1e10f;

    // Search all valid fractional clock dividers to find the best (TOP, clkdiv) pair.
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
                    int_div = 256; // break outer loop
                    break;         // break inner loop
                }
            }
        }
    }

    pwm_set_enabled(slice, false);
    pwm_set_wrap(slice, best_top);
    pwm_set_clkdiv(slice, best_div);
    pwm_set_chan_level(slice, ch, (uint32_t)(best_top * duty));
    pwm_set_enabled(slice, true);

    enabled[channel] = true;
    actual_freqs[channel] = (float)sys_clk / (best_div * (best_top + 1));
    wraps[channel] = (uint16_t)best_top;

    return true;
}

void hw_pwm_set_duty(uint channel, float duty) {
    if (channel >= HW_PWM_COUNT) return;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    uint gpio = hw_pwm_gpios[channel];
    uint slice = pwm_gpio_to_slice_num(gpio);
    uint ch = pwm_gpio_to_channel(gpio);

    uint16_t wrap = wraps[channel];
    pwm_set_chan_level(slice, ch, (uint32_t)(wrap * duty));
    duties[channel] = duty;
}

void hw_pwm_enable(uint channel, bool enable) {
    if (channel >= HW_PWM_COUNT) return;

    uint gpio = hw_pwm_gpios[channel];
    uint slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_enabled(slice, enable);
    enabled[channel] = enable;
}

float hw_pwm_get_actual_freq(uint channel) {
    if (channel >= HW_PWM_COUNT) return 0.0f;
    return actual_freqs[channel];
}

float hw_pwm_get_duty(uint channel) {
    if (channel >= HW_PWM_COUNT) return 0.0f;
    return duties[channel];
}

bool hw_pwm_is_enabled(uint channel) {
    if (channel >= HW_PWM_COUNT) return false;
    return enabled[channel];
}

uint32_t hw_pwm_get_pulse_count(uint channel) {
    if (channel >= HW_PWM_COUNT) return 0;
    return pulse_counts[channel];
}

void hw_pwm_set_pulse_count(uint channel, uint32_t count) {
    if (channel >= HW_PWM_COUNT) return;
    pulse_counts[channel] = count;
}

void hw_pwm_reset_pulse_count(uint channel) {
    hw_pwm_set_pulse_count(channel, 0);
}
