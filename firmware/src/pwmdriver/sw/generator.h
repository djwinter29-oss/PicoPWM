/**
 * @file generator.h
 * @brief Core 1 software PWM generator backend for the logical `pwmdriver` layer.
 */

#ifndef PWMDRIVER_SW_GENERATOR_H
#define PWMDRIVER_SW_GENERATOR_H

#include "../pwm_driver.h"

/** @brief Initialize the software PWM generator backend and its repeating timer. */
void sw_gen_init(void);

/**
 * @brief Apply one logical software-PWM channel update.
 * @param channel Backend-local software PWM channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`.
 * @return `true` when the backend accepted the request.
 */
bool sw_gen_set_freq(uint channel, uint32_t freq_hz, uint8_t duty);

/**
 * @brief Read the realized state for one software PWM backend channel.
 * @param channel Backend-local software PWM channel index.
 * @param state Caller-owned destination for the realized state.
 * @return `true` when the channel exists and the state was copied.
 */
bool sw_gen_get(uint channel, pwm_driver_state_t *state);

#endif