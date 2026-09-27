/**
 * @file generator_math.h
 * @brief Pure helper functions shared by PWM generator backends.
 */

#ifndef PWMDRIVER_GENERATOR_MATH_H
#define PWMDRIVER_GENERATOR_MATH_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/** @brief Convert a duty percentage to a bounded PWM compare level. */
static inline uint32_t pwm_generator_level_from_duty(uint32_t period_counts, uint8_t duty_percent) {
    uint32_t level;

    if (duty_percent == 0u) {
        return 0u;
    }
    if (duty_percent >= 100u) {
        return period_counts;
    }

    level = (uint32_t)(((uint64_t)period_counts * duty_percent + 50u) / 100u);
    return level > period_counts ? period_counts : level;
}

/** @brief Resolve whether a software PWM request is a static output. */
static inline bool pwm_generator_resolve_static(uint32_t frequency_hz, uint8_t duty_percent,
                                                bool *high_out, uint8_t *realized_duty_out) {
    if (high_out == NULL || realized_duty_out == NULL ||
        (frequency_hz != 0u && duty_percent != 0u && duty_percent < 100u)) {
        return false;
    }

    *high_out = duty_percent >= 100u;
    *realized_duty_out = *high_out ? 100u : 0u;
    return true;
}

#endif