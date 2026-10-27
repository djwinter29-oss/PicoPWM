/**
 * @file software_math.h
 * @brief Pure static-output helper for the software PWM generator.
 */

#ifndef PWMDRIVER_SOFTWARE_MATH_H
#define PWMDRIVER_SOFTWARE_MATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Resolve whether a software PWM request is a static output. */
static inline bool pwm_generator_resolve_static(uint32_t frequency_hz, uint8_t duty_percent, bool *high_out,
                                                uint8_t *realized_duty_out) {
    if (high_out == NULL || realized_duty_out == NULL ||
        (frequency_hz != 0u && duty_percent != 0u && duty_percent < 100u)) {
        return false;
    }

    *high_out = duty_percent >= 100u;
    *realized_duty_out = *high_out ? 100u : 0u;
    return true;
}

#endif
