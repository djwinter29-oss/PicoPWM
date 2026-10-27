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

/**
 * @brief PIO generator duty program produced for one period and duty request.
 *
 * The state machine counts `period_count + 1` iterations. The pin goes high
 * when the countdown equals @ref match and stays high for @ref high_iterations.
 */
typedef struct {
    uint32_t match;            /**< Threshold written to PIO register X. */
    uint32_t high_iterations;  /**< Iterations the pin is high during one period. */
    uint32_t total_iterations; /**< Iterations in one period (`period_count + 1`). */
} pwm_pio_duty_program_t;

/** @brief Program the sticky-high PIO match value for one duty request. */
static inline void pwm_pio_program_duty(uint16_t period_count, uint8_t duty_percent, pwm_pio_duty_program_t *out) {
    uint32_t total_iterations = (uint32_t)period_count + 1u;
    uint32_t high_iterations;

    if (out == NULL) {
        return;
    }

    high_iterations = pwm_generator_level_from_duty(total_iterations, duty_percent);
    out->total_iterations = total_iterations;
    out->high_iterations = high_iterations;
    /* Y never equals `total_iterations`, so a zero-width pulse stays low. */
    out->match = high_iterations == 0u ? total_iterations : high_iterations - 1u;
}

/**
 * @brief Count high iterations of the PIO generator's sticky-high countdown.
 * @param period_count Period loaded into ISR.
 * @param match Match threshold loaded into X.
 * @return Iterations spent high, including the matching iteration.
 */
static inline uint32_t pwm_pio_simulate_high_iterations(uint16_t period_count, uint32_t match) {
    uint32_t high_iterations = 0u;
    bool pin_high = false;
    uint32_t y = period_count;

    while (true) {
        if (y == match) {
            pin_high = true;
        }
        if (pin_high) {
            high_iterations++;
        }
        if (y == 0u) {
            break;
        }
        y--;
    }

    return high_iterations;
}

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