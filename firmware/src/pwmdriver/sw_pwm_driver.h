/**
 * @file sw_pwm_driver.h
 * @brief Core 1 software PWM backend for the logical `pwmdriver` layer.
 */

#ifndef PWMDRIVER_SW_PWM_DRIVER_H
#define PWMDRIVER_SW_PWM_DRIVER_H

#include "pwm_driver.h"

/** @brief Initialize the software PWM backend and its repeating timer. */
void sw_pwm_driver_init(void);

/**
 * @brief Apply one logical software-PWM channel update.
 * @param channel Backend-local software PWM channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in the normalized range `[0.0, 1.0]`.
 * @return `true` when the backend accepted the request.
 */
bool sw_pwm_driver_set_freq(uint channel, float freq_hz, float duty);

/**
 * @brief Read the realized state for one software PWM backend channel.
 * @param channel Backend-local software PWM channel index.
 * @param state Caller-owned destination for the realized state.
 * @return `true` when the channel exists and the state was copied.
 */
bool sw_pwm_driver_get(uint channel, pwm_driver_state_t *state);

#endif