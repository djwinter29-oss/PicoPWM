#include "app/device_service/device_config.h"
#include "hardware/flash.h"
#include "hardware/i2c.h"
#include "pico/multicore.h"

#include <assert.h>
#include <string.h>

i2c_inst_t i2c1_inst;
int test_multicore_lockout_victim_ready;

void system_watchdog_kick(void) {}

int main(void) {
    device_config_t config;

    test_flash_reset();

    /* Invalid storage falls back to the conservative default. */
    assert(!device_config_load_target(&config));
    assert(device_config_validate(&config));
    assert(config.i2c_address == 0x40u);

    /* Flash erase stays refused until Core 1 has armed the lockout victim. */
    device_config_default(&config);
    config.i2c_address = 0x41u;
    assert(!device_config_save_target(&config));
    multicore_lockout_victim_init();

    /* A second save becomes the newest slot and supersedes the first. */
    device_config_default(&config);
    config.i2c_address = 0x41u;
    assert(device_config_save_target(&config));
    config.i2c_address = 0x42u;
    assert(device_config_save_target(&config));
    assert(device_config_load_target(&config));
    assert(config.i2c_address == 0x42u);

    /* Corrupting the newest slot must preserve the previous valid target. */
    test_flash[FLASH_SECTOR_SIZE] ^= 0x01u;
    assert(device_config_load_target(&config));
    assert(config.i2c_address == 0x41u);

    test_flash_reset();

    {
        device_config_t running;
        device_config_t target;
        device_config_t snapshot;

        device_config_default(&running);
        target = running;
        target.i2c_address = 0x42u;
        assert(device_config_init_state(&running, &target));
        assert(device_config_get_running(&snapshot));
        assert(snapshot.i2c_address == 0x40u);
        assert(device_config_get_target(&snapshot));
        assert(snapshot.i2c_address == 0x42u);

        target.i2c_address = 0x07u;
        assert(!device_config_init_state(&running, &target));
        assert(!device_config_set_i2c_address(0x78u));
    }

    device_config_default(NULL);
    assert(!device_config_validate(NULL));
    assert(!device_config_load_target(NULL));
    assert(!device_config_save_target(NULL));
    config.i2c_address = 0x01u;
    assert(!device_config_save_target(&config));
    assert(!device_config_get_running(NULL));
    assert(!device_config_get_target(NULL));
    assert(!device_config_init_state(NULL, NULL));
    assert(device_config_set_i2c_address(0x41u));
    assert(device_config_set_bank(PWM_DRIVER_CONFIG_BANK_A, PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR));
    assert(device_config_set_bank(PWM_DRIVER_CONFIG_BANK_B, PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR));
    assert(device_config_set_bank(PWM_DRIVER_CONFIG_BANK_C, PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR));
    assert(!device_config_set_bank(PWM_DRIVER_CONFIG_BANK_COUNT, PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR));
    assert(!device_config_set_bank(PWM_DRIVER_CONFIG_BANK_A, (pwm_driver_config_bank_role_t)9u));

    return 0;
}
