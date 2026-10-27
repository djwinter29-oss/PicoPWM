/**
 * @file generator.c
 * @brief Hardware PWM generator backend implementation for the logical `pwm` layer.
 */

#include "hardware_generator.h"

#include "../pwm_driver.h"
#include "../pwm_driver_internal.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "../pwm_math.h"
#include "hardware_math.h"

/** @brief Cached system clock used by the hardware generator timing search. */
static uint32_t hw_gen_sys_clk_hz = 0u;

static void hw_gen_publish_state(uint channel, uint32_t realized_freq_hz, uint8_t realized_duty);

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

/** @brief Apply one realized static hardware output state and publish the matching shared snapshot. */
static void hw_gen_apply_static_state(uint channel, uint8_t realized_duty) {
    uint gpio = hardware_pwm_config.channels[channel].gpio;
    uint slice = pwm_gpio_to_slice_num(gpio);
    uint ch = pwm_gpio_to_channel(gpio);

    pwm_set_enabled(slice, false);
    pwm_set_chan_level(slice, ch, 0u);
    hw_gen_drive_static_level(gpio, realized_duty >= 100u);
    hw_gen_publish_state(channel, 0u, realized_duty);
}

/** @brief Publish one hardware backend realized state snapshot into the shared logical cache. */
static void hw_gen_publish_state(uint channel, uint32_t realized_freq_hz, uint8_t realized_duty) {
    pwm_state_t state;

    state.freq_hz = realized_freq_hz;
    state.duty = realized_duty;

    pwm_driver_store_applied_state(((uint)PWM_BANK_A * PWM_DRIVER_CONFIG_BANK_SIZE + (uint)(channel)), &state);
}

/** @copydoc hw_gen_init */
bool hw_gen_init(void) {
    hw_gen_sys_clk_hz = clock_get_hz(clk_sys);

    for (int i = 0; i < PWM_DRIVER_CONFIG_BANK_SIZE; i++) {
        uint gpio = hardware_pwm_config.channels[i].gpio;
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

    return true;
}

/** @copydoc hw_gen_set */
bool hw_gen_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    uint32_t best_top;
    uint16_t best_div_x16;
    uint32_t level;
    bool static_high;
    uint8_t static_duty;

    if (channel >= PWM_DRIVER_CONFIG_BANK_SIZE)
        return false;
    if (duty > 100u)
        duty = 100u;

    uint gpio = hardware_pwm_config.channels[channel].gpio;
    uint slice = pwm_gpio_to_slice_num(gpio);
    uint ch = pwm_gpio_to_channel(gpio);

    if (pwm_generator_resolve_static(freq_hz, duty, &static_high, &static_duty)) {
        hw_gen_apply_static_state(channel, static_duty);
        return true;
    }

    if (!pwm_hw_find_timing(hw_gen_sys_clk_hz, freq_hz, &best_top, &best_div_x16)) {
        return false;
    }

    hw_gen_bind_pwm_pin(gpio);

    pwm_set_enabled(slice, false);
    pwm_set_wrap(slice, best_top);
    pwm_set_clkdiv_int_frac(slice, best_div_x16 / 16u, best_div_x16 % 16u);
    level = pwm_generator_level_from_duty(best_top + 1u, duty);
    pwm_set_chan_level(slice, ch, level);
    pwm_set_enabled(slice, true);

    /* ponytail: The generator caches clk_sys at init time. The ceiling is runtime reclocking:
     * if firmware changes the system clock after hw_gen_init(), realized frequency publication
     * drifts until the backend is reinitialized. That tradeoff is acceptable now because the
     * current firmware sets the system clock once at startup. If runtime reclocking is added
     * later, refresh this cached value or read the live clock here.
     */
    hw_gen_publish_state(channel, pwm_hw_realized_freq_hz(hw_gen_sys_clk_hz, best_top, best_div_x16),
                         pwm_realized_duty(level, best_top + 1u));

    return true;
}
