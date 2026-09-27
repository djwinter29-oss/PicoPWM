#include "pwmdriver/pwm_driver_internal.h"
#include "hardware/flash.h"

#include <assert.h>
#include <string.h>

int main(void) {
    pwm_driver_config_t config;

    test_flash_reset();

    /* Invalid storage falls back to the conservative default. */
    assert(!pwm_driver_config_load_target(&config));
    assert(pwm_driver_config_validate_target(&config));
    assert(config.i2c_address == 0x40u);

    /* Flash erase stays refused until Core 1 has armed the lockout victim. */
    pwm_driver_config_default(&config);
    config.i2c_address = 0x41u;
    assert(!pwm_driver_config_lockout_victim_ready());
    assert(!pwm_driver_config_save_target(&config));
    pwm_driver_config_arm_lockout_victim();
    assert(pwm_driver_config_lockout_victim_ready());

    /* A second save becomes the newest slot and supersedes the first. */
    pwm_driver_config_default(&config);
    config.i2c_address = 0x41u;
    assert(pwm_driver_config_save_target(&config));
    config.i2c_address = 0x42u;
    assert(pwm_driver_config_save_target(&config));
    assert(pwm_driver_config_load_target(&config));
    assert(config.i2c_address == 0x42u);

    /* Corrupting the newest slot must preserve the previous valid target. */
    test_flash[FLASH_SECTOR_SIZE] ^= 0x01u;
    assert(pwm_driver_config_load_target(&config));
    assert(config.i2c_address == 0x41u);

    test_flash_reset();

    {
        pwm_driver_config_t running;
        pwm_driver_config_t target;
        pwm_driver_config_t snapshot;

        pwm_driver_config_default(&running);
        target = running;
        target.i2c_address = 0x42u;
        assert(pwm_driver_config_init_state(&running, &target));
        assert(pwm_driver_config_get_running(&snapshot));
        assert(snapshot.i2c_address == 0x40u);
        assert(pwm_driver_config_get_target(&snapshot));
        assert(snapshot.i2c_address == 0x42u);

        target.i2c_address = 0x07u;
        assert(!pwm_driver_config_init_state(&running, &target));
        assert(!pwm_driver_config_set_i2c_address(0x78u));
    }

    /* Startup roles populate the fixed channel table before the driver launches Core 1. */

    /* Configure all three banks at startup. */
    const pwm_driver_config_bank_role_t roles[PWM_DRIVER_CONFIG_BANK_COUNT] = {
        PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR,
        PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
        PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR,
    };
    const pwm_driver_config_bank_backend_t backends[PWM_DRIVER_CONFIG_BANK_COUNT] = {
        PWM_DRIVER_CONFIG_BANK_BACKEND_HW,
        PWM_DRIVER_CONFIG_BANK_BACKEND_PIO,
        PWM_DRIVER_CONFIG_BANK_BACKEND_SW,
    };
    assert(pwm_driver_configure_table(backends, roles));

    {
        pwm_driver_config_bank_backend_t invalid_backends[PWM_DRIVER_CONFIG_BANK_COUNT] = {
            PWM_DRIVER_CONFIG_BANK_BACKEND_PIO,
            PWM_DRIVER_CONFIG_BANK_BACKEND_PIO,
            PWM_DRIVER_CONFIG_BANK_BACKEND_SW,
        };
        assert(!pwm_driver_configure_table(invalid_backends, roles));
        invalid_backends[0] = PWM_DRIVER_CONFIG_BANK_BACKEND_HW;
        invalid_backends[1] = PWM_DRIVER_CONFIG_BANK_BACKEND_HW;
        assert(!pwm_driver_configure_table(invalid_backends, roles));
        invalid_backends[1] = PWM_DRIVER_CONFIG_BANK_BACKEND_PIO;
        invalid_backends[2] = PWM_DRIVER_CONFIG_BANK_BACKEND_HW;
        assert(!pwm_driver_configure_table(invalid_backends, roles));
    }
    {
        const pwm_driver_config_channel_t *profile = pwm_driver_config_get_channel(0u);
        assert(profile->gpio == 1u);
        assert(profile->backend == PWM_DRIVER_CONFIG_BACKEND_HW_GENERATOR);
        assert(profile->backend_channel == 0u);
        assert(profile->min_frequency_hz > 0u);
        assert(profile->max_frequency_hz >= profile->min_frequency_hz);
        assert(profile->accuracy_ppm > 0u);
        assert(pwm_driver_config_frequency_supported(0u, 0u));
        assert(pwm_driver_config_frequency_supported(0u, profile->min_frequency_hz));
        assert(pwm_driver_config_frequency_supported(0u, profile->max_frequency_hz));
        assert(!pwm_driver_config_frequency_supported(0u, profile->max_frequency_hz + 1u));
        assert(!pwm_driver_config_frequency_supported(0u, profile->min_frequency_hz - 1u));

        uint gpio = 0u;
        assert(pwm_driver_config_get_gpio(profile->backend, profile->backend_channel, &gpio));
        assert(gpio == profile->gpio);

        uint channel = 0u;
        assert(pwm_driver_config_get_logical_channel(profile->backend, profile->backend_channel, &channel));
        assert(channel == 0u);
    }
    /* PIO bank is configured as monitor: fixed companion slice-A GPIOs, channels 8..15. */
    {
        const pwm_driver_config_channel_t *profile = pwm_driver_config_get_channel(8u);
        assert(profile->gpio == 0u);
        assert(profile->backend == PWM_DRIVER_CONFIG_BACKEND_PIO_MONITOR);
        assert(profile->direction == PWM_DRIVER_CONFIG_DIRECTION_INPUT);
    }

    /* SW bank is configured as generator: remaining GPIOs, channels 16..23. */
    {
        const pwm_driver_config_channel_t *profile = pwm_driver_config_get_channel(16u);
        assert(profile->gpio == 16u);
        assert(profile->backend == PWM_DRIVER_CONFIG_BACKEND_SW_GENERATOR);
    }

    assert(pwm_driver_config_validate());
    assert(pwm_driver_config_get_channel(PWM_DRIVER_CONFIG_CHANNEL_COUNT) == NULL);

    {
        pwm_driver_config_channel_t invalid[PWM_DRIVER_CONFIG_CHANNEL_COUNT];
        for (uint channel = 0u; channel < PWM_DRIVER_CONFIG_CHANNEL_COUNT; ++channel) {
            invalid[channel] = *pwm_driver_config_get_channel(channel);
        }

        invalid[0].backend = (pwm_driver_config_backend_t)99u;
        assert(!pwm_driver_config_validate_table(invalid, PWM_DRIVER_CONFIG_CHANNEL_COUNT));
        invalid[0] = *pwm_driver_config_get_channel(0u);

        invalid[0].direction = PWM_DRIVER_CONFIG_DIRECTION_DISABLED;
        invalid[0].capabilities = PWM_DRIVER_CONFIG_CAP_READ;
        assert(!pwm_driver_config_validate_table(invalid, PWM_DRIVER_CONFIG_CHANNEL_COUNT));
        invalid[0] = *pwm_driver_config_get_channel(0u);

        invalid[0].direction = PWM_DRIVER_CONFIG_DIRECTION_INPUT;
        invalid[0].backend = PWM_DRIVER_CONFIG_BACKEND_SW_MONITOR;
        invalid[0].capabilities = 0u;
        assert(!pwm_driver_config_validate_table(invalid, PWM_DRIVER_CONFIG_CHANNEL_COUNT));
        invalid[0] = *pwm_driver_config_get_channel(0u);

        invalid[1].gpio = invalid[0].gpio;
        assert(!pwm_driver_config_validate_table(invalid, PWM_DRIVER_CONFIG_CHANNEL_COUNT));
        invalid[1] = *pwm_driver_config_get_channel(1u);
        invalid[0].gpio = 23u;
        invalid[0].backend = PWM_DRIVER_CONFIG_BACKEND_HW_GENERATOR;
        assert(!pwm_driver_config_validate_table(invalid, PWM_DRIVER_CONFIG_CHANNEL_COUNT));
    }

    return 0;
}
