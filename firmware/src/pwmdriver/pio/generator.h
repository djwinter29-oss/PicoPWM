/**
 * @file generator.h
 * @brief Core 1 PIO PWM generator backend for the logical `pwmdriver` layer.
 */

#ifndef PWMDRIVER_PIO_GENERATOR_H
#define PWMDRIVER_PIO_GENERATOR_H

#include "../pwm_driver.h"

/** @brief Initialize the PIO PWM generator backend, programs, and IRQ handling. */
void pio_pwm_generator_init(void);

/**
 * @brief Apply one logical PIO PWM generator channel update.
 * @param channel Backend-local PIO PWM channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in the normalized range `[0.0, 1.0]`.
 * @return `true` when the backend accepted the request.
 */
bool pio_pwm_generator_set_freq(uint channel, float freq_hz, float duty);

/**
 * @brief Read the realized state for one PIO PWM generator backend channel.
 * @param channel Backend-local PIO PWM channel index.
 * @param state Caller-owned destination for the realized state.
 * @return `true` when the channel exists and the state was copied.
 */
bool pio_pwm_generator_get(uint channel, pwm_driver_state_t *state);

#endif