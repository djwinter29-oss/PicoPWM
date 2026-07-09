/**
 * @file hw_pwm_driver.h
 * @brief Core 1 hardware PWM backend for the logical `pwmdriver` layer.
 */

#ifndef PWMDRIVER_HW_PWM_DRIVER_H
#define PWMDRIVER_HW_PWM_DRIVER_H

#include "pwm_driver.h"

/** @brief Initialize the hardware PWM backend and its wrap IRQ handling. */
void hw_pwm_driver_init(void);

/**
 * @brief Apply one logical hardware-PWM channel update.
 * @param channel Backend-local hardware PWM channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in the normalized range `[0.0, 1.0]`.
 * @return `true` when the backend accepted the request.
 */
bool hw_pwm_driver_set_freq(uint channel, float freq_hz, float duty);

/**
 * @brief Read the realized state for one hardware PWM backend channel.
 * @param channel Backend-local hardware PWM channel index.
 * @param state Caller-owned destination for the realized state.
 * @return `true` when the channel exists and the state was copied.
 */
bool hw_pwm_driver_get(uint channel, pwm_driver_state_t *state);

#endif