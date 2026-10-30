// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

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

/**
 * @brief Realized duty in percent from the integer high and total step counts that program the output.
 * @param high_steps Steps the output is high in one period.
 * @param total_steps Steps in one period; zero yields 0.
 * @return Rounded duty in percent, clamped to `[0, 100]`.
 */
static inline uint8_t pwm_realized_duty(uint32_t high_steps, uint32_t total_steps) {
    uint64_t duty;

    if (total_steps == 0u) {
        return 0u;
    }
    duty = ((uint64_t)high_steps * 100u + total_steps / 2u) / total_steps;
    return duty > 100u ? 100u : (uint8_t)duty;
}

#endif
