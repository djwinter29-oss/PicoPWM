/**
 * @file pwm_driver.h
 * @brief Public logical-channel and multicore PWM control interfaces.
 */

#ifndef PWMDRIVER_PWM_DRIVER_H
#define PWMDRIVER_PWM_DRIVER_H

#include "pico/stdlib.h"
#include "pwmdriver/pwm_driver_config.h"

#include <stdint.h>

/** @brief Hardware PWM backend channel capacity. */
#define HW_PWM_COUNT 8
/** @brief PIO PWM backend channel capacity. */
#define PIO_PWM_DRIVER_COUNT 8

/** @brief Total logical PWM channel count across all backends. */
#define PWM_DRIVER_CHANNEL_COUNT PWM_DRIVER_CONFIG_CHANNEL_COUNT

/** @brief Role selected for one fixed PWM bank at startup. */
typedef pwm_driver_config_bank_role_t pwm_driver_bank_role_t;

/** @brief Complete startup configuration for the three fixed PWM banks. */
typedef struct {
    pwm_driver_config_bank_backend_t bank_a_backend; /**< Bank A backend family: HW or SW. */
    pwm_driver_config_bank_backend_t bank_b_backend; /**< Bank B backend family: PIO or SW. */
    pwm_driver_config_bank_backend_t bank_c_backend; /**< Bank C backend family: SW only. */
    pwm_driver_bank_role_t bank_a_role;              /**< Bank A role. */
    pwm_driver_bank_role_t bank_b_role;              /**< Bank B role. */
    pwm_driver_bank_role_t bank_c_role;              /**< Bank C role. */
    uint8_t i2c_address;                             /**< Running/target 7-bit I2C address. */
} pwm_driver_config_t;

/** @brief Return the safe default startup configuration. */
void pwm_driver_config_default(pwm_driver_config_t *config);
/** @brief Validate backend and role combinations for the fixed A/B/C map. */
bool pwm_driver_config_validate_target(const pwm_driver_config_t *config);
/** @brief Load the persisted target; returns the default when storage is invalid. */
bool pwm_driver_config_load_target(pwm_driver_config_t *config);
/** @brief Persist a validated target configuration in flash. */
bool pwm_driver_config_save_target(const pwm_driver_config_t *config);
/** @brief Publish validated running and target configuration snapshots for transport status. */
bool pwm_driver_config_init_state(const pwm_driver_config_t *running, const pwm_driver_config_t *target);
/** @brief Copy the target configuration snapshot. */
bool pwm_driver_config_get_target(pwm_driver_config_t *config);
/** @brief Copy the immutable running configuration snapshot. */
bool pwm_driver_config_get_running(pwm_driver_config_t *config);
/** @brief Update one target bank without changing running hardware. */
bool pwm_driver_config_set_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_backend_t backend,
                                pwm_driver_config_bank_role_t role);
/** @brief Update the target I2C address without changing the running address. */
bool pwm_driver_config_set_i2c_address(uint8_t address);

/** @brief Result codes returned by shared PWM control operations. */
typedef enum {
    PWM_DRIVER_RESULT_OK = 0,       /**< The request completed successfully. */
    PWM_DRIVER_RESULT_BUSY,         /**< Another command was already pending or executing. */
    PWM_DRIVER_RESULT_INVALID,      /**< The caller supplied an invalid channel or value. */
    PWM_DRIVER_RESULT_UNAVAILABLE,  /**< The requested operation is not available in the current context. */
    PWM_DRIVER_RESULT_TIMEOUT,      /**< Core 1 did not publish a reply before the command timeout. */
    PWM_DRIVER_RESULT_APPLY_FAILED, /**< The backend rejected the admitted request. */
} pwm_driver_result_t;

/** @brief Realized logical state snapshot for one PWM channel. */
typedef struct {
    uint32_t freq_hz;     /**< Realized output frequency in Hz. */
    uint8_t duty;         /**< Realized duty cycle in percent in the range `[0, 100]`. */
    uint32_t pulse_count; /**< Monotonic generated-period count from power-on; the PIO backend reports this as an
                             estimated period count rather than a hardware-counted edge total. */
} pwm_driver_state_t;

/**
 * @brief Configure all banks, launch Core 1 backend ownership, and start the PWM runtime.
 * @param config Immutable startup configuration for all three banks.
 * @return `true` when the configuration was accepted and Core 1 was launched.
 */
bool pwm_driver_init(const pwm_driver_config_t *config);

/**
 * @brief Return whether Core 1 started the PWM mailbox runtime.
 * @return `true` once the driver has initialized the configured backends and
 *         can accept channel commands.
 */
bool pwm_driver_is_ready(void);

/** @brief Return whether Core 1 startup failed while initializing a selected backend. */
bool pwm_driver_startup_failed(void);

#endif