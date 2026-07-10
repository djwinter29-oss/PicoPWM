/**
 * @file generator.h
 * @brief Core 1 PIO generator backend for the logical `pwmdriver` layer.
 */

#ifndef PWMDRIVER_PIO_GENERATOR_H
#define PWMDRIVER_PIO_GENERATOR_H

#include "../pwm_driver.h"

/** @brief Initialize the PIO generator backend and its PIO programs. */
void pio_gen_init(void);

/**
 * @brief Apply one logical PIO generator channel update.
 * @param channel Backend-local PIO generator channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`.
 * @return `true` when the backend accepted the request.
 */
bool pio_gen_set_freq(uint channel, uint32_t freq_hz, uint8_t duty);

/**
 * @brief Read the realized state for one PIO generator backend channel.
 * @param channel Backend-local PIO generator channel index.
 * @param state Caller-owned destination for the realized state.
 * @return `true` when the channel exists and the state was copied.
 * @note This backend getter returns the cached base `pulse_count`. The shared
 *       `pwm_driver_get()` path owns live PIO pulse extrapolation.
 */
bool pio_gen_get(uint channel, pwm_driver_state_t *state);

#endif