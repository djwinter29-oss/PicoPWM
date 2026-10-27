/**
 * @file pio_math.h
 * @brief Pure duty-to-match helper for the PIO PWM generator.
 */

#ifndef PWMDRIVER_PIO_MATH_H
#define PWMDRIVER_PIO_MATH_H

#include <stddef.h>
#include <stdint.h>

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

    if (duty_percent == 0u) {
        high_iterations = 0u;
    } else if (duty_percent >= 100u) {
        high_iterations = total_iterations;
    } else {
        high_iterations = (uint32_t)(((uint64_t)total_iterations * duty_percent + 50u) / 100u);
        if (high_iterations > total_iterations) {
            high_iterations = total_iterations;
        }
    }
    out->total_iterations = total_iterations;
    out->high_iterations = high_iterations;
    /* Y never equals `total_iterations`, so a zero-width pulse stays low. */
    out->match = high_iterations == 0u ? total_iterations : high_iterations - 1u;
}

#endif
