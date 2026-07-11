/**
 * @file generator.h
 * @brief Core 1 hardware PWM generator backend for the logical `pwmdriver` layer.
 */

#ifndef PWMDRIVER_HW_GENERATOR_H
#define PWMDRIVER_HW_GENERATOR_H

#include "../pwm_driver.h"

/** @brief Initialize the hardware PWM generator backend. */
void hw_gen_init(void);

/**
 * @brief Apply one logical hardware-PWM channel update.
 * @param channel Backend-local hardware PWM channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`.
 * @return `true` when the backend accepted the request.
 */
bool hw_gen_set_freq(uint channel, uint32_t freq_hz, uint8_t duty);

/**
 * @brief Read the realized state for one hardware PWM backend channel.
 * @param channel Backend-local hardware PWM channel index.
 * @param state Caller-owned destination for the realized state.
 * @return `true` when the channel exists and the state was copied.
 */
bool hw_gen_get(uint channel, pwm_driver_state_t *state);

#endif