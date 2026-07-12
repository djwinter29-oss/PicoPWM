/**
 * @file pwm_driver_internal.h
 * @brief Internal backend-facing helpers shared within `pwmdriver`.
 */

#ifndef PWMDRIVER_PWM_DRIVER_INTERNAL_H
#define PWMDRIVER_PWM_DRIVER_INTERNAL_H

#include "pwm_driver.h"

#include <stdint.h>

/**
 * @brief Accumulate additional pulses from one cached base count and elapsed time.
 * @param pulse_count Cached base pulse count.
 * @param freq_hz Realized running frequency in Hz.
 * @param pulse_ref_us Reference timestamp paired with @p pulse_count.
 * @param now_us Current timestamp used for elapsed-time accumulation.
 * @return Saturating pulse count advanced by elapsed time at @p freq_hz.
 */
static inline uint32_t pwm_driver_accumulate_pulse_count(uint32_t pulse_count, uint32_t freq_hz, uint64_t pulse_ref_us, uint64_t now_us) {
	uint64_t total_pulses;

	if (freq_hz == 0u || now_us <= pulse_ref_us) {
		return pulse_count;
	}

	total_pulses = (uint64_t)pulse_count + ((now_us - pulse_ref_us) * (uint64_t)freq_hz) / 1000000u;
	if (total_pulses > UINT32_MAX) {
		return UINT32_MAX;
	}

	return (uint32_t)total_pulses;
}

/** @brief GPIO pin map for the hardware PWM backend. */
extern const uint PWM_HW_GPIO_PINS[];
/** @brief GPIO pin map for the software PWM backend. */
extern const uint PWM_SW_GPIO_PINS[];
/** @brief GPIO pin map for the PIO PWM backend. */
extern const uint PWM_PIO_GPIO_PINS[];

/**
 * @brief Submit one cross-core logical channel update.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`; values above `100` are clamped.
 * @return Result code for the admitted command attempt.
 * @note This is the internal Core 0 command-ingress API underneath `control_iface`.
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
 * @param pulse_ref_us Timestamp paired with `state->pulse_count`.
 * @note Callers must ensure the source state cannot change while this snapshot is being copied.
 */
void pwm_driver_store_applied_state_coherent(uint channel, const pwm_driver_state_t *state, uint64_t pulse_ref_us);

/**
 * @brief Publish one updated pulse counter for the logical channel snapshot cache.
 * @param channel Logical channel index.
 * @param pulse_count New monotonic pulse counter value.
 */
void pwm_driver_store_pulse_count(uint channel, uint32_t pulse_count);

#endif