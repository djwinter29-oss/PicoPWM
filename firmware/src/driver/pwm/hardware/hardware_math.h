/**
 * @file hardware_math.h
 * @brief Pure duty and timing helpers for the hardware PWM generator.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#ifndef PWMDRIVER_HARDWARE_MATH_H
#define PWMDRIVER_HARDWARE_MATH_H

#include <stdbool.h>
#include <stddef.h>
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

/** @brief Smallest supported hardware PWM divider in sixteenth-step units. */
#define PWM_HW_MIN_DIV_X16 16u
/** @brief Largest supported hardware PWM divider in sixteenth-step units. */
#define PWM_HW_MAX_DIV_X16 (255u * 16u + 15u)
/** @brief Largest supported wrap value plus one. */
#define PWM_HW_MAX_PERIOD_COUNTS 65536u

/** @brief Rounded realized frequency for one wrap value and divider, or 0 for an invalid divider. */
static inline uint32_t pwm_hw_realized_freq_hz(uint32_t sys_clk_hz, uint32_t top, uint16_t div_x16) {
    uint64_t denominator = (uint64_t)div_x16 * ((uint64_t)top + 1u);

    return denominator == 0u ? 0u : (uint32_t)((((uint64_t)sys_clk_hz * 16u) + (denominator / 2u)) / denominator);
}

/**
 * @brief Find the wrap value and divider whose realized frequency is closest to @p freq_hz.
 * @param sys_clk_hz System clock in Hz.
 * @param freq_hz Requested frequency in Hz.
 * @param best_top Selected wrap value.
 * @param best_div_x16 Selected divider in sixteenth-step units.
 * @return `true` when the frequency is inside the timing envelope; `false` below the minimum, above half the
 *         system clock, or for zero.
 * @note The search walks every divider from the smallest wrap-legal value through @ref PWM_HW_MAX_DIV_X16
 *       and stops early on an exact match. A set command runs this once; it is not an interrupt path.
 */
static inline bool pwm_hw_find_timing(uint32_t sys_clk_hz, uint32_t freq_hz, uint32_t *best_top,
                                      uint16_t *best_div_x16) {
    uint64_t numerator = (uint64_t)sys_clk_hz * 16u;
    uint32_t start_div;
    uint32_t best_delta_hz = UINT32_MAX;

    if (freq_hz == 0u || best_top == NULL || best_div_x16 == NULL) {
        return false;
    }

    start_div = (uint32_t)((numerator + ((uint64_t)freq_hz * 65536u) - 1u) / ((uint64_t)freq_hz * 65536u));
    if (start_div < PWM_HW_MIN_DIV_X16) {
        start_div = PWM_HW_MIN_DIV_X16;
    }
    if (start_div > PWM_HW_MAX_DIV_X16) {
        return false;
    }

    for (uint32_t div_x16 = start_div; div_x16 <= PWM_HW_MAX_DIV_X16; div_x16++) {
        uint64_t denominator = (uint64_t)freq_hz * div_x16;
        uint64_t period_counts = (numerator + (denominator / 2u)) / denominator;
        uint32_t actual_hz;
        uint32_t delta_hz;

        if (period_counts < 2u || period_counts > PWM_HW_MAX_PERIOD_COUNTS) {
            continue;
        }

        actual_hz = pwm_hw_realized_freq_hz(sys_clk_hz, (uint32_t)(period_counts - 1u), (uint16_t)div_x16);
        delta_hz = actual_hz > freq_hz ? actual_hz - freq_hz : freq_hz - actual_hz;
        if (delta_hz < best_delta_hz) {
            best_delta_hz = delta_hz;
            *best_top = (uint32_t)(period_counts - 1u);
            *best_div_x16 = (uint16_t)div_x16;
            if (delta_hz == 0u) {
                break;
            }
        }
    }

    return best_delta_hz != UINT32_MAX;
}

#endif
