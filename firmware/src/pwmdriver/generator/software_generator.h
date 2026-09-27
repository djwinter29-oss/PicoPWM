/**
 * @file generator.h
 * @brief Core 1 software PWM generator backend for the logical `pwmdriver` layer.
 */

#ifndef PWMDRIVER_SW_GENERATOR_H
#define PWMDRIVER_SW_GENERATOR_H

#include "../pwm_driver.h"

/** @brief Maximum supported software PWM frequency in Hz. */
#define SW_GEN_MAX_FREQ_HZ 1000u

/** @brief Initialize the software PWM generator backend and its repeating timer. */
void sw_gen_init(void);

/**
 * @brief Apply one logical software-PWM channel update.
 * @param channel Backend-local software PWM channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`.
 * @return `true` when the backend accepted the request.
 */
bool sw_gen_set(uint channel, uint32_t freq_hz, uint8_t duty);

/** @brief Restore the software PWM backend channels to the shared logical default state. */
bool sw_gen_restore_defaults(void);

#endif