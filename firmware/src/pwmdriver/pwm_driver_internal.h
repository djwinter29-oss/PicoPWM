/**
 * @file pwm_driver_internal.h
 * @brief Internal backend-facing helpers shared within `pwmdriver`.
 */

#ifndef PWMDRIVER_PWM_DRIVER_INTERNAL_H
#define PWMDRIVER_PWM_DRIVER_INTERNAL_H

#include "pwm_driver.h"

#include <stdint.h>

/** @brief GPIO pin map for the hardware PWM backend. */
extern const uint PWM_HW_GPIO_PINS[];
/** @brief GPIO pin map for the software PWM backend. */
extern const uint PWM_SW_GPIO_PINS[];
/** @brief GPIO pin map for the PIO PWM backend. */
extern const uint PWM_PIO_GPIO_PINS[];

/** @brief Round one backend frequency to the exported integer-Hz representation. */
static inline uint32_t pwm_driver_freq_hz_from_float(float freq_hz) {
	if (freq_hz <= 0.0f) {
		return 0u;
	}

	if (freq_hz >= (float)UINT32_MAX) {
		return UINT32_MAX;
	}

	return (uint32_t)(freq_hz + 0.5f);
}

/** @brief Convert one normalized backend duty value into the exported percent representation. */
static inline uint8_t pwm_driver_duty_percent_from_float(float duty) {
	if (duty <= 0.0f) {
		return 0u;
	}

	if (duty >= 1.0f) {
		return 100u;
	}

	return (uint8_t)(duty * 100.0f + 0.5f);
}

/** @brief Clamp one exported duty percent and convert it into the normalized backend representation. */
static inline float pwm_driver_duty_ratio_from_percent(uint8_t duty_percent) {
	if (duty_percent > 100u) {
		duty_percent = 100u;
	}

	return (float)duty_percent / 100.0f;
}

/**
 * @brief Publish one newly applied logical channel snapshot.
 * @param channel Logical channel index.
 * @param state Caller-owned realized state snapshot.
 */
void pwm_driver_store_applied_state(uint channel, const pwm_driver_state_t *state);

/**
 * @brief Publish one updated pulse counter for the logical channel snapshot cache.
 * @param channel Logical channel index.
 * @param pulse_count New monotonic pulse counter value.
 */
void pwm_driver_store_pulse_count(uint channel, uint32_t pulse_count);

#endif