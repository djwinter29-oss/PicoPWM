/**
 * @file boot_config.h
 * @brief Persisted boot settings: system clock, bank roles and I2C address.
 *
 * One RAM copy is loaded from flash at boot. Setters edit it, `boot_config_save()` persists it, and the new values
 * apply at the next boot; the running values come from the live modules. Core 0 only.
 */

#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include "driver/pwm/pwm_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief User-selected system clock profile; flash stores the ID and the CLI and system clock use MHz. */
typedef enum {
    BOOT_CONFIG_SYSCLK_DEFAULT = 0, /**< Keep the SDK default clock (SYS_CLK_KHZ); used when nothing is saved. */
    BOOT_CONFIG_SYSCLK_150MHZ = 1,  /**< 150 MHz. */
    BOOT_CONFIG_SYSCLK_200MHZ = 2,  /**< 200 MHz. */
    BOOT_CONFIG_SYSCLK_250MHZ = 3,  /**< 250 MHz. */
} boot_config_sysclk_t;

/** @brief Read the saved settings from flash into RAM; defaults when nothing valid is stored. Returns true when found.
 */
bool boot_config_load(void);
/**
 * @brief Persist the RAM settings in flash.
 * @note Fails closed until Core 1 has become the flash-programming lockout victim.
 */
bool boot_config_save(void);

/** @brief Configured role of one bank. */
pwm_role_t boot_config_get_bank(pwm_bank_t bank);
/** @brief Update one bank role in RAM. */
bool boot_config_set_bank(pwm_bank_t bank, pwm_role_t role);

/** @brief Configured 7-bit I2C slave address. */
uint8_t boot_config_get_i2c_address(void);
/** @brief Update the I2C address in RAM (0x08..0x77). */
bool boot_config_set_i2c_address(uint8_t address);

/** @brief Configured system clock profile. */
boot_config_sysclk_t boot_config_get_sysclk(void);
/** @brief Update the system clock profile in RAM; false (unchanged) for an unknown profile. */
bool boot_config_set_sysclk(boot_config_sysclk_t profile);

/** @brief Configured system clock in MHz, or 0 to keep the SDK default clock (SYS_CLK_KHZ). */
static inline uint16_t boot_config_get_sysclk_mhz(void) {
    static const uint16_t mhz[] = {0u, 150u, 200u, 250u};
    const unsigned profile = (unsigned)boot_config_get_sysclk();
    return profile < sizeof(mhz) / sizeof(mhz[0]) ? mhz[profile] : 0u;
}

#endif
