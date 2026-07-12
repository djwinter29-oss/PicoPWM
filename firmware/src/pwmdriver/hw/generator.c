/**
 * @file generator.c
 * @brief Hardware PWM generator backend implementation for the logical `pwmdriver` layer.
 */

#include "generator.h"

#include "../pwm_driver.h"
#include "../pwm_driver_internal.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

/** @brief Cached system clock used by the hardware generator timing search. */
static uint32_t hw_gen_sys_clk_hz = 0u;

/** @brief Smallest supported hardware PWM divider in sixteenth-step units. */
#define HW_GEN_MIN_DIV_X16 16u
/** @brief Largest supported hardware PWM divider in sixteenth-step units. */
#define HW_GEN_MAX_DIV_X16 (255u * 16u + 15u)
/** @brief Largest supported wrap value plus one. */
#define HW_GEN_MAX_PERIOD_COUNTS 65536u
/** @brief Local seeded-search window size in sixteenth-step divider units. */
#define HW_GEN_DIV_SEARCH_WINDOW_X16 128u

/**
 * @brief Convert a duty percent into the hardware compare level for one wrap value.
 * @param top Active wrap value.
 * @param duty_percent Requested duty in percent in the range `[0, 100]`.
 * @return Compare level accepted by the Pico SDK PWM API.
 */
static uint32_t hw_pwm_level_from_duty(uint32_t top, uint8_t duty_percent) {
    if (duty_percent == 0u) {
        return 0;
    }
    if (duty_percent >= 100u) {
        return top + 1u;
    }

    uint32_t level = (uint32_t)(((uint64_t)(top + 1u) * duty_percent + 50u) / 100u);
    if (level > top + 1u) {
        level = top + 1u;
    }
    return level;
}

/** @brief Bind one hardware generator pin back to PWM mode. */
static void hw_gen_bind_pwm_pin(uint gpio) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);
}

/** @brief Drive one hardware generator pin as a static GPIO level outside PWM mode. */
static void hw_gen_drive_static_level(uint gpio, bool high) {
    gpio_set_function(gpio, GPIO_FUNC_SIO);
    gpio_set_dir(gpio, GPIO_OUT);
    gpio_put(gpio, high);
}

/** @brief Resolve one hardware static-output request to the realized driven level. */
static uint8_t hw_gen_static_duty(uint8_t duty_percent) {
    return duty_percent >= 100u ? 100u : 0u;
}

/** @brief Apply one realized static hardware output state and publish the matching shared snapshot. */
static void hw_gen_apply_static_state(uint channel, uint8_t realized_duty) {
    uint gpio = PWM_HW_GPIO_PINS[channel];
    uint slice = pwm_gpio_to_slice_num(gpio);
    uint ch = pwm_gpio_to_channel(gpio);

    pwm_set_enabled(slice, false);
    pwm_set_chan_level(slice, ch, 0u);
    hw_gen_drive_static_level(gpio, realized_duty >= 100u);
    hw_gen_publish_state(channel, 0u, realized_duty);
}

/** @brief Publish one hardware backend realized state snapshot into the shared logical cache. */
static void hw_gen_publish_state(uint channel, uint32_t realized_freq_hz, uint8_t realized_duty) {
    pwm_driver_state_t state = {
        .freq_hz = realized_freq_hz,
        .duty = realized_duty,
        .pulse_count = 0u,
    };

    pwm_driver_store_applied_state(HW_PWM_CHANNEL_BASE + channel, &state);
}

/** @brief Return the smallest supported hardware PWM frequency in Hz for the current clock plan. */
static uint32_t hw_gen_min_freq_hz(void) {
    uint64_t denominator = (uint64_t)HW_GEN_MAX_DIV_X16 * (uint64_t)HW_GEN_MAX_PERIOD_COUNTS;
    uint64_t numerator = (uint64_t)hw_gen_sys_clk_hz * 16u;

    return (uint32_t)((numerator + denominator - 1u) / denominator);
}

/** @brief Return whether one requested hardware frequency is within the backend timing envelope. */
static bool hw_gen_freq_supported(uint32_t freq_hz) {
    uint32_t min_hw_hz;
    uint32_t max_hw_hz;

    if (freq_hz == 0u) {
        return false;
    }

    min_hw_hz = hw_gen_min_freq_hz();
    max_hw_hz = hw_gen_sys_clk_hz / 2u;
    return freq_hz >= min_hw_hz && freq_hz <= max_hw_hz;
}

/** @brief Return one realized frequency from a wrap value and divider in sixteenth-step units. */
static uint32_t hw_gen_realized_freq_hz(uint32_t top, uint16_t div_x16) {
    uint64_t denominator = (uint64_t)div_x16 * (uint64_t)(top + 1u);
    uint64_t numerator = (uint64_t)hw_gen_sys_clk_hz * 16u;

    if (denominator == 0u) {
        return 0u;
    }

    return (uint32_t)((numerator + (denominator / 2u)) / denominator);
}

/** @brief Evaluate one timing candidate and keep it when it improves the current best error. */
static void hw_gen_consider_timing(uint32_t freq_hz, uint32_t div_x16, uint32_t *best_top, uint16_t *best_div_x16, uint32_t *best_delta_hz) {
    uint64_t numerator = (uint64_t)hw_gen_sys_clk_hz * 16u;
    uint64_t denominator = (uint64_t)freq_hz * div_x16;
    uint64_t period_counts;
    uint32_t top;
    uint32_t actual_freq_hz;
    uint32_t delta_hz;

    if (denominator == 0u) {
        return;
    }

    period_counts = (numerator + (denominator / 2u)) / denominator;
    if (period_counts < 2u || period_counts > HW_GEN_MAX_PERIOD_COUNTS) {
        return;
    }

    top = (uint32_t)(period_counts - 1u);
    actual_freq_hz = hw_gen_realized_freq_hz(top, (uint16_t)div_x16);
    delta_hz = actual_freq_hz > freq_hz ? actual_freq_hz - freq_hz : freq_hz - actual_freq_hz;
    if (delta_hz < *best_delta_hz) {
        *best_delta_hz = delta_hz;
        *best_top = top;
        *best_div_x16 = (uint16_t)div_x16;
    }
}

/** @brief Seed the local divider search window for one requested hardware frequency. */
static void hw_gen_seed_div_window(uint32_t freq_hz, uint32_t *start_div_x16, uint32_t *end_div_x16) {
    uint64_t min_div_x16_numerator = ((uint64_t)hw_gen_sys_clk_hz * 16u) + ((uint64_t)freq_hz * 65536u) - 1u;
    uint32_t min_div_x16 = (uint32_t)(min_div_x16_numerator / ((uint64_t)freq_hz * 65536u));

    if (min_div_x16 < HW_GEN_MIN_DIV_X16) {
        min_div_x16 = HW_GEN_MIN_DIV_X16;
    }

    *start_div_x16 = min_div_x16;
    *end_div_x16 = min_div_x16 + HW_GEN_DIV_SEARCH_WINDOW_X16;
    if (*end_div_x16 > HW_GEN_MAX_DIV_X16) {
        *end_div_x16 = HW_GEN_MAX_DIV_X16;
    }
}

/**
 * @brief Find one hardware timing pair for the requested output frequency.
 * @param freq_hz Requested frequency in Hz.
 * @param best_top Caller-owned destination for the chosen wrap value.
 * @param best_div_x16 Caller-owned destination for the chosen divider in sixteenth-step units.
 * @return `true` when the requested frequency is supported by the hardware backend.
 */
static bool hw_gen_find_timing(uint32_t freq_hz, uint32_t *best_top, uint16_t *best_div_x16) {
    uint32_t start_div_x16;
    uint32_t end_div_x16;
    uint32_t best_delta_hz = UINT32_MAX;

    if (best_top == NULL || best_div_x16 == NULL || !hw_gen_freq_supported(freq_hz)) {
        return false;
    }

    *best_top = 255u;
    *best_div_x16 = HW_GEN_MIN_DIV_X16;

    /* ponytail: This seeded integer search only probes a local divider window above the
     * smallest divider that keeps top in range. The ceiling is that an unusually distant
     * divider candidate could beat the local window for a future clock plan. That tradeoff is
     * acceptable now because the current clock plan and frequency range keep good candidates
     * near the minimum valid divider. If that assumption breaks later, widen the window or
     * fall back to an exhaustive integer sweep.
     */
    hw_gen_seed_div_window(freq_hz, &start_div_x16, &end_div_x16);

    for (uint32_t div_x16 = start_div_x16; div_x16 <= end_div_x16; div_x16++) {
        hw_gen_consider_timing(freq_hz, div_x16, best_top, best_div_x16, &best_delta_hz);
        if (best_delta_hz == 0u) {
            return true;
        }
    }

    return best_delta_hz != UINT32_MAX;
}

/** @copydoc hw_gen_init */
void hw_gen_init(void) {
    hw_gen_sys_clk_hz = clock_get_hz(clk_sys);

    for (int i = 0; i < HW_PWM_COUNT; i++) {
        uint gpio = PWM_HW_GPIO_PINS[i];
        hw_gen_bind_pwm_pin(gpio);

        uint slice = pwm_gpio_to_slice_num(gpio);
        uint ch = pwm_gpio_to_channel(gpio);

        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv_int_frac(&cfg, 1u, 0u);
        pwm_config_set_wrap(&cfg, 255);
        pwm_init(slice, &cfg, true);

        pwm_set_chan_level(slice, ch, 0);
        pwm_set_enabled(slice, false);

    }
}

/** @copydoc hw_gen_set */
bool hw_gen_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    uint32_t best_top;
    uint16_t best_div_x16;
    uint32_t realized_freq_hz;
    uint8_t static_duty;

    if (channel >= HW_PWM_COUNT) return false;
    if (duty > 100u) duty = 100u;

    uint gpio = PWM_HW_GPIO_PINS[channel];
    uint slice = pwm_gpio_to_slice_num(gpio);
    uint ch = pwm_gpio_to_channel(gpio);

    static_duty = hw_gen_static_duty(duty);

    if (freq_hz == 0u || duty == 0u || duty >= 100u) {
        hw_gen_apply_static_state(channel, static_duty);
        return true;
    }

    if (!hw_gen_find_timing(freq_hz, &best_top, &best_div_x16)) {
        return false;
    }

    hw_gen_bind_pwm_pin(gpio);

    pwm_set_enabled(slice, false);
    pwm_set_wrap(slice, best_top);
    pwm_set_clkdiv_int_frac(slice, best_div_x16 / 16u, best_div_x16 % 16u);
    pwm_set_chan_level(slice, ch, hw_pwm_level_from_duty(best_top, duty));
    pwm_set_enabled(slice, true);

    /* ponytail: The generator caches clk_sys at init time. The ceiling is runtime reclocking:
     * if firmware changes the system clock after hw_gen_init(), realized frequency publication
     * drifts until the backend is reinitialized. That tradeoff is acceptable now because the
     * current firmware sets the system clock once at startup. If runtime reclocking is added
     * later, refresh this cached value or read the live clock here.
     */
    realized_freq_hz = hw_gen_realized_freq_hz(best_top, best_div_x16);
    hw_gen_publish_state(channel, realized_freq_hz, duty);

    return true;
}

/** @copydoc hw_gen_restore_defaults */
bool hw_gen_restore_defaults(void) {
    for (uint channel = 0; channel < HW_PWM_COUNT; channel++) {
        hw_gen_apply_static_state(channel, 0u);
    }

    return true;
}
