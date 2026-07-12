/**
 * @file pwm_driver.h
 * @brief Public logical-channel and multicore PWM control interfaces.
 */

#ifndef PWMDRIVER_PWM_DRIVER_H
#define PWMDRIVER_PWM_DRIVER_H

#include "pico/stdlib.h"

#include <stdint.h>

/** @brief Logical hardware PWM channel count. */
#define HW_PWM_COUNT 8
/** @brief Logical PIO PWM channel count. */
#define PIO_PWM_DRIVER_COUNT 8
/** @brief Logical software PWM channel count. */
#define SW_PWM_COUNT 8

/** @brief Logical base index for hardware PWM channels. */
#define HW_PWM_CHANNEL_BASE 0
/** @brief Logical base index for PIO PWM channels. */
#define PIO_PWM_CHANNEL_BASE (HW_PWM_CHANNEL_BASE + HW_PWM_COUNT)
/** @brief Logical base index for software PWM channels. */
#define SW_PWM_CHANNEL_BASE (PIO_PWM_CHANNEL_BASE + PIO_PWM_DRIVER_COUNT)
/** @brief Total logical PWM channel count across all backends. */
#define PWM_DRIVER_CHANNEL_COUNT (HW_PWM_COUNT + PIO_PWM_DRIVER_COUNT + SW_PWM_COUNT)

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
 * @brief Launch Core 1 backend ownership and start the PWM driver runtime.
 */
void pwm_driver_launch(void);

/**
 * @brief Return whether Core 1 finished backend initialization.
 * @return `true` once the PWM driver runtime is ready to accept commands.
 */
bool pwm_driver_is_ready(void);

#endif