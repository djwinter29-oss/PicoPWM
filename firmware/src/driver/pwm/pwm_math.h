/**
 * @file pwm_math.h
 * @brief Pure static-output rule shared by every PWM generator backend.
 */

#ifndef PWMDRIVER_PWM_MATH_H
#define PWMDRIVER_PWM_MATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Resolve whether a generator request is a static output instead of a running PWM.
 * @param frequency_hz Requested frequency in Hz; zero is always static.
 * @param duty_percent Requested duty in percent.
 * @param high_out Receives the level to drive when the request is static.
 * @param realized_duty_out Receives the realized duty, `0` or `100`, when the request is static.
 * @return `true` when the request is static; `false` when a PWM must run.
 */
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
