/**
 * @file pio_pwm_driver.h
 * @brief Core 1 PIO PWM backend for the logical `pwmdriver` layer.
 */

#ifndef PWMDRIVER_PIO_PWM_DRIVER_H
#define PWMDRIVER_PIO_PWM_DRIVER_H

#include "pwm_driver.h"

/** @brief Initialize the PIO PWM backend, programs, and IRQ handling. */
void pio_pwm_driver_init(void);

/**
 * @brief Apply one logical PIO-PWM channel update.
 * @param channel Backend-local PIO PWM channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in the normalized range `[0.0, 1.0]`.
 * @return `true` when the backend accepted the request.
 */
bool pio_pwm_driver_set_freq(uint channel, float freq_hz, float duty);

/**
 * @brief Read the realized state for one PIO PWM backend channel.
 * @param channel Backend-local PIO PWM channel index.
 * @param state Caller-owned destination for the realized state.
 * @return `true` when the channel exists and the state was copied.
 */
bool pio_pwm_driver_get(uint channel, pwm_driver_state_t *state);

#endif