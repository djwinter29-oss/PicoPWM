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
bool pio_gen_set(uint channel, uint32_t freq_hz, uint8_t duty);

/** @brief Restore the PIO PWM backend channels to the shared logical default state. */
bool pio_gen_restore_defaults(void);

/**
 * @brief Finalize one published PIO readback snapshot for Core 0 consumers.
 * @param channel Backend-local PIO generator channel index.
 * @param state Caller-owned snapshot to finalize in place.
 * @param pulse_ref_us Reference timestamp paired with `state->pulse_count`.
 */
void pio_gen_finalize_readback(uint channel, pwm_driver_state_t *state, uint64_t pulse_ref_us);

#endif