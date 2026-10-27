#include "platform/boot_config.h"
#include "hardware/flash.h"
#include "hardware/i2c.h"
#include "pico/multicore.h"

#include <assert.h>
#include <string.h>

i2c_inst_t i2c1_inst;
int test_multicore_lockout_victim_ready;

void system_watchdog_kick(void) {}

int main(void) {
    test_flash_reset();

    /* Invalid storage falls back to the conservative default. */
    assert(!boot_config_load());
    assert(boot_config_get_i2c_address() == 0x40u);
    assert(boot_config_get_sysclk_mhz() == 0u);
    assert(boot_config_get_bank(PWM_BANK_A) == PWM_ROLE_MONITOR);
    assert(boot_config_get_bank(PWM_BANK_COUNT) == PWM_ROLE_MONITOR);

    /* Setters validate before changing the RAM copy. */
    assert(!boot_config_set_i2c_address(0x07u));
    assert(!boot_config_set_i2c_address(0x78u));
    assert(!boot_config_set_sysclk((boot_config_sysclk_t)9));
    assert(!boot_config_set_bank(PWM_BANK_COUNT, PWM_ROLE_GENERATOR));
    assert(!boot_config_set_bank(PWM_BANK_A, (pwm_role_t)9u));
    assert(boot_config_set_i2c_address(0x41u));
    assert(boot_config_set_sysclk(BOOT_CONFIG_SYSCLK_200MHZ));
    assert(boot_config_set_bank(PWM_BANK_B, PWM_ROLE_GENERATOR));
    assert(boot_config_get_i2c_address() == 0x41u);
    assert(boot_config_get_sysclk_mhz() == 200u);

    /* Flash erase stays refused until Core 1 has armed the lockout victim. */
    assert(!boot_config_save());
    multicore_lockout_victim_init();
    assert(boot_config_save());

    /* A reload from flash returns the saved values. */
    boot_config_set_i2c_address(0x50u);
    assert(boot_config_load());
    assert(boot_config_get_i2c_address() == 0x41u);
    assert(boot_config_get_sysclk_mhz() == 200u);
    assert(boot_config_get_bank(PWM_BANK_B) == PWM_ROLE_GENERATOR);
    assert(boot_config_get_bank(PWM_BANK_A) == PWM_ROLE_MONITOR);

    /* A corrupted record falls back to the defaults. */
    test_flash[0] ^= 0x01u;
    assert(!boot_config_load());
    assert(boot_config_get_i2c_address() == 0x40u);

    return 0;
}
