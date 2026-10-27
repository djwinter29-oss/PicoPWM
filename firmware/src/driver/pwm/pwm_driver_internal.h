/**
 * @file pwm_driver_internal.h
 * @brief Internal backend-facing helpers shared within `pwm`.
 */

#ifndef PWMDRIVER_PWM_DRIVER_INTERNAL_H
#define PWMDRIVER_PWM_DRIVER_INTERNAL_H

#include "pwm_driver.h"

#include <stdint.h>

/**
 * @brief Submit one cross-core logical channel update.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`; values above `100` are clamped.
 * @return Result code for the admitted command attempt.
 * @note This is the internal Core 0 command-ingress API underneath `device`.
 */
pwm_driver_result_t pwm_driver_set(uint channel, uint32_t freq_hz, uint8_t duty);

/**
 * @brief Read the latest published realized state for one logical channel.
 * @param channel Logical channel index.
 * @param state Caller-owned destination for the realized channel state.
 * @return `true` when the channel exists and the state was copied.
 */
bool pwm_driver_get(uint channel, pwm_driver_state_t *state);

/**
 * @brief Restore all logical channels to the shared default state.
 * @return Result code for the admitted command attempt.
 */
pwm_driver_result_t pwm_driver_restore_defaults(void);

/**
 * @brief Publish one newly applied logical channel snapshot.
 * @param channel Logical channel index.
 * @param state Caller-owned realized state snapshot.
 */
void pwm_driver_store_applied_state(uint channel, const pwm_driver_state_t *state);

/**
 * @brief Publish one newly applied logical channel snapshot when the caller already owns a coherent update boundary.
 * @param channel Logical channel index.
 * @param state Caller-owned realized state snapshot.
 * @note Callers must ensure the source state cannot change while this snapshot is being copied.
 */
void pwm_driver_store_applied_state_coherent(uint channel, const pwm_driver_state_t *state);

#endif
