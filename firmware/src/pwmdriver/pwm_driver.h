/**
 * @file pwm_driver.h
 * @brief Public logical-channel and multicore PWM control interfaces.
 */

#ifndef PWMDRIVER_PWM_DRIVER_H
#define PWMDRIVER_PWM_DRIVER_H

#include "pico/stdlib.h"
#include "pwmdriver/channel_config/channel_config.h"

#include <stdint.h>

/** @brief Hardware PWM backend channel capacity. */
#define HW_PWM_COUNT 8
/** @brief PIO PWM backend channel capacity. */
#define PIO_PWM_DRIVER_COUNT 8

/** @brief Total logical PWM channel count across all backends. */
#define PWM_DRIVER_CHANNEL_COUNT PWM_PROFILE_CHANNEL_COUNT

/** @brief Role selected for one fixed PWM bank at startup. */
typedef pwm_profile_bank_role_t pwm_driver_bank_role_t;

/** @brief Optional backend-specific startup options reserved for future tuning. */
typedef struct {
    const void *options; /**< Backend-owned immutable option block, or `NULL` for defaults. */
} pwm_driver_bank_config_t;

/** @brief Complete startup configuration for the three fixed PWM banks. */
typedef struct {
    pwm_driver_bank_config_t bank_a; /**< Bank A configuration. */
    pwm_driver_bank_config_t bank_b; /**< Bank B configuration. */
    pwm_driver_bank_config_t bank_c; /**< Bank C configuration. */
    pwm_profile_bank_backend_t bank_a_backend; /**< Bank A backend family: HW or SW. */
    pwm_profile_bank_backend_t bank_b_backend; /**< Bank B backend family: PIO or SW. */
    pwm_profile_bank_backend_t bank_c_backend; /**< Bank C backend family: SW only. */
    pwm_driver_bank_role_t bank_a_role; /**< Bank A role. */
    pwm_driver_bank_role_t bank_b_role; /**< Bank B role. */
    pwm_driver_bank_role_t bank_c_role; /**< Bank C role. */
} pwm_driver_config_t;

/** @brief Result codes returned by shared PWM control operations. */
typedef enum {
    PWM_DRIVER_RESULT_OK = 0, /**< The request completed successfully. */
    PWM_DRIVER_RESULT_BUSY, /**< Another command was already pending or executing. */
    PWM_DRIVER_RESULT_INVALID, /**< The caller supplied an invalid channel or value. */
    PWM_DRIVER_RESULT_UNAVAILABLE, /**< The requested operation is not available in the current context. */
    PWM_DRIVER_RESULT_TIMEOUT, /**< Core 1 did not publish a reply before the command timeout. */
    PWM_DRIVER_RESULT_APPLY_FAILED, /**< The backend rejected the admitted request. */
} pwm_driver_result_t;

/** @brief Realized logical state snapshot for one PWM channel. */
typedef struct {
    uint32_t freq_hz; /**< Realized output frequency in Hz. */
    uint8_t duty; /**< Realized duty cycle in percent in the range `[0, 100]`. */
    uint32_t pulse_count; /**< Monotonic generated-period count from power-on; the PIO backend reports this as an estimated period count rather than a hardware-counted edge total. */
} pwm_driver_state_t;

/**
 * @brief Configure all banks, launch Core 1 backend ownership, and start the PWM runtime.
 * @param config Immutable startup configuration for all three banks.
 * @return `true` when the configuration was accepted and Core 1 was launched.
 */
bool pwm_driver_init(const pwm_driver_config_t *config);

/** @brief Legacy launch entry point; use pwm_driver_init() for new code. */
void pwm_driver_launch(void);

/**
 * @brief Return whether Core 1 started the PWM mailbox runtime.
 * @return `true` once the driver has initialized the configured backends and
 *         can accept channel commands.
 */
bool pwm_driver_is_ready(void);

/** @brief Return whether Core 1 startup failed while initializing a selected backend. */
bool pwm_driver_startup_failed(void);

#endif