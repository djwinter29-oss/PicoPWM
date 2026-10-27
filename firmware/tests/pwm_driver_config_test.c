#include "driver/pwm/pwm_driver_internal.h"
#include "hardware/flash.h"
#include "hardware/i2c.h"

#include <assert.h>
#include <string.h>

i2c_inst_t i2c1_inst;

void system_watchdog_kick(void) {}

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
    assert(!pwm_driver_config_is_monitor());
    assert(strcmp(pwm_driver_config_channel_name(0u), "?") == 0);
    assert(!pwm_driver_config_frequency_supported(99u, 1u));
    assert(!pwm_driver_config_channel_role(0u, NULL));

    pwm_driver_config_default(NULL);
    assert(!pwm_driver_config_validate_target(NULL));
    assert(!pwm_driver_config_load_target(NULL));
    assert(!pwm_driver_config_save_target(NULL));
    config.i2c_address = 0x01u;
    assert(!pwm_driver_config_save_target(&config));
    assert(!pwm_driver_config_get_running(NULL));
    assert(!pwm_driver_config_get_target(NULL));
    assert(!pwm_driver_config_init_state(NULL, NULL));
    assert(pwm_driver_config_set_i2c_address(0x41u));
    assert(pwm_driver_config_set_bank(PWM_DRIVER_CONFIG_BANK_A, PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR));
    assert(pwm_driver_config_set_bank(PWM_DRIVER_CONFIG_BANK_B, PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR));
    assert(pwm_driver_config_set_bank(PWM_DRIVER_CONFIG_BANK_C, PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR));
    assert(!pwm_driver_config_set_bank(PWM_DRIVER_CONFIG_BANK_COUNT, PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR));
    assert(!pwm_driver_config_set_bank(PWM_DRIVER_CONFIG_BANK_A, (pwm_driver_config_bank_role_t)9u));

    /* Configure all three banks at startup. */
    const pwm_driver_config_bank_role_t roles[PWM_DRIVER_CONFIG_BANK_COUNT] = {
        PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR,
        PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
        PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR,
    };
    assert(!pwm_driver_configure_table(NULL));
    {
        pwm_driver_config_bank_role_t bad_roles[PWM_DRIVER_CONFIG_BANK_COUNT] = {
            (pwm_driver_config_bank_role_t)9u,
            PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
            PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR,
        };
        assert(!pwm_driver_configure_table(bad_roles));
    }
    assert(pwm_driver_configure_table(roles));

    {
        pwm_driver_config_bank_role_t role;
        assert(pwm_driver_config_channel_role(0u, &role) && role == PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR);
        assert(pwm_driver_config_channel_role(8u, &role) && role == PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR);
        assert(!pwm_driver_config_channel_role(PWM_DRIVER_CONFIG_CHANNEL_COUNT, &role));
        assert(strcmp(pwm_driver_config_channel_name(0u), "HW") == 0);
        assert(strcmp(pwm_driver_config_channel_name(8u), "PIO-MON") == 0);
        assert(strcmp(pwm_driver_config_channel_name(16u), "SW") == 0);
        assert(strcmp(pwm_driver_config_channel_name(PWM_DRIVER_CONFIG_CHANNEL_COUNT), "?") == 0);
        assert(pwm_driver_config_frequency_supported(0u, 0u));
        assert(pwm_driver_config_frequency_supported(0u, 10u));
        assert(pwm_driver_config_frequency_supported(0u, 1000000u));
        assert(!pwm_driver_config_frequency_supported(0u, 1000001u));
        assert(!pwm_driver_config_frequency_supported(0u, 9u));
        assert(pwm_driver_config_frequency_supported(16u, 1000u));
        assert(!pwm_driver_config_frequency_supported(16u, 1001u));
    }

    assert(pwm_driver_config_validate());

    assert(!pwm_driver_configure_table(roles));
    assert(pwm_driver_config_is_monitor());

    return 0;
}
