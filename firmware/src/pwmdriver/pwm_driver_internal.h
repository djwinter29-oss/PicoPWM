/**
 * @file pwm_driver_internal.h
 * @brief Internal backend-facing helpers shared within `pwmdriver`.
 */

#ifndef PWMDRIVER_PWM_DRIVER_INTERNAL_H
#define PWMDRIVER_PWM_DRIVER_INTERNAL_H

#include "pwm_driver.h"

/** @brief GPIO pin map for the hardware PWM backend. */
extern const uint PWM_HW_GPIO_PINS[];
/** @brief GPIO pin map for the software PWM backend. */
extern const uint PWM_SW_GPIO_PINS[];
/** @brief GPIO pin map for the PIO PWM backend. */
extern const uint PWM_PIO_GPIO_PINS[];

/**
 * @brief Publish one newly applied logical channel snapshot.
 * @param channel Logical channel index.
 * @param state Caller-owned realized state snapshot.
 */
void pwm_driver_store_applied_state(uint channel, const pwm_driver_state_t *state);

/**
 * @brief Publish one updated pulse counter for the logical channel snapshot cache.
 * @param channel Logical channel index.
 * @param pulse_count New monotonic pulse counter value.
 */
void pwm_driver_store_pulse_count(uint channel, uint32_t pulse_count);

#endif