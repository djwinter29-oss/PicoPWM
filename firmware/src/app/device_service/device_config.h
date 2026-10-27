/**
 * @file device_config.h
 * @brief Persisted device settings: bank roles and I2C address, with running and target copies.
 */

#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include "driver/pwm/pwm_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Device settings applied at boot. */
typedef struct {
    pwm_driver_config_bank_role_t bank_a_role; /**< Bank A role. */
    pwm_driver_config_bank_role_t bank_b_role; /**< Bank B role. */
    pwm_driver_config_bank_role_t bank_c_role; /**< Bank C role. */
    uint8_t i2c_address;                       /**< 7-bit I2C slave address. */
} device_config_t;

/** @brief Return the safe default configuration. */
void device_config_default(device_config_t *config);
/** @brief Validate roles and I2C address. */
bool device_config_validate(const device_config_t *config);
/** @brief Load the persisted target; returns the default when storage is invalid. */
bool device_config_load_target(device_config_t *config);
/**
 * @brief Persist a validated target configuration in flash.
 * @note Fails closed until Core 1 has become the flash-programming lockout victim.
 */
bool device_config_save_target(const device_config_t *config);
/** @brief Persist the current target configuration. */
bool device_config_save(void);
/** @brief Return whether the target differs from the saved copy (defaults when none is saved). */
bool device_config_target_dirty(void);
/** @brief Publish validated running and target configuration snapshots. */
bool device_config_init_state(const device_config_t *running, const device_config_t *target);
/** @brief Copy the target configuration snapshot. */
bool device_config_get_target(device_config_t *config);
/** @brief Copy the immutable running configuration snapshot. */
bool device_config_get_running(device_config_t *config);
/** @brief Update one target bank role without changing running hardware. */
bool device_config_set_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_role_t role);
/** @brief Update the target I2C address without changing the running address. */
bool device_config_set_i2c_address(uint8_t address);

#endif
