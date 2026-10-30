/**
 * @file pio_math.h
 * @brief Pure duty and timing helpers for the PIO PWM generator.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#ifndef PWMDRIVER_PIO_MATH_H
#define PWMDRIVER_PIO_MATH_H

#include <stdbool.h>
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

/** @brief Maximum quantized PIO clock divider in units of `1/256`. */
#define PWM_PIO_MAX_CLKDIV_X256 ((65535u * 256u) + 255u)
/** @brief Largest PIO loop count that fits the 16-bit period field. */
#define PWM_PIO_MAX_PERIOD_COUNT 65535u

/** @brief Steady-state PIO instruction count for one PWM period. */
static inline uint32_t pwm_pio_cycles_per_period(uint16_t period_count) {
    return 3u * ((uint32_t)period_count + 1u) + 2u;
}

/** @brief Rounded realized output frequency for one timing configuration, or 0 for an invalid divider. */
static inline uint32_t pwm_pio_realized_freq_hz(uint32_t sys_clk_hz, uint16_t period_count, uint32_t clkdiv_x256) {
    uint64_t denominator = (uint64_t)clkdiv_x256 * pwm_pio_cycles_per_period(period_count);

    return denominator == 0u ? 0u : (uint32_t)((((uint64_t)sys_clk_hz * 256u) + (denominator / 2u)) / denominator);
}

/**
 * @brief Pick a PIO period count and divider whose realized frequency is closest to @p freq_hz.
 * @param sys_clk_hz System clock in Hz.
 * @param freq_hz Requested frequency in Hz.
 * @param period_count_out Selected loop count.
 * @param clkdiv_x256_out Selected divider in units of `1/256`.
 * @return `true` when a realizable configuration exists.
 * @note ponytail: only the estimated period count and its two neighbours are tried. The divider quantization
 *       dominates the error, so wider searches gain little; widen the window if the host sweep test regresses.
 */
static inline bool pwm_pio_find_timing(uint32_t sys_clk_hz, uint32_t freq_hz, uint16_t *period_count_out,
                                       uint32_t *clkdiv_x256_out) {
    uint32_t ideal_cycles;
    uint32_t estimate;
    uint32_t best_error = UINT32_MAX;
    uint32_t best_actual = 0u;

    if (freq_hz == 0u || period_count_out == NULL || clkdiv_x256_out == NULL) {
        return false;
    }

    ideal_cycles = (sys_clk_hz + (freq_hz / 2u)) / freq_hz;
    if (ideal_cycles < pwm_pio_cycles_per_period(1u)) {
        return false;
    }

    estimate = ideal_cycles <= 5u ? 1u : (ideal_cycles - 5u) / 3u;
    if (estimate < 1u) {
        estimate = 1u;
    }
    if (estimate > PWM_PIO_MAX_PERIOD_COUNT) {
        estimate = PWM_PIO_MAX_PERIOD_COUNT;
    }

    for (uint32_t period = estimate > 1u ? estimate - 1u : 1u; period <= estimate + 1u; ++period) {
        uint64_t denominator;
        uint64_t clkdiv_x256;
        uint32_t actual;
        uint32_t error;

        if (period > PWM_PIO_MAX_PERIOD_COUNT) {
            break;
        }
        denominator = (uint64_t)freq_hz * pwm_pio_cycles_per_period((uint16_t)period);
        clkdiv_x256 = (((uint64_t)sys_clk_hz * 256u) + (denominator / 2u)) / denominator;
        if (clkdiv_x256 < 256u || clkdiv_x256 > PWM_PIO_MAX_CLKDIV_X256) {
            continue;
        }

        actual = pwm_pio_realized_freq_hz(sys_clk_hz, (uint16_t)period, (uint32_t)clkdiv_x256);
        error = actual > freq_hz ? actual - freq_hz : freq_hz - actual;
        /* Periods ascend, so `<=` prefers the larger loop count on a tie. */
        if (error <= best_error) {
            best_error = error;
            best_actual = actual;
            *period_count_out = (uint16_t)period;
            *clkdiv_x256_out = (uint32_t)clkdiv_x256;
        }
    }

    return best_actual != 0u;
}

/** @brief What to do with one PIO TX word while the FIFO may be full. */
typedef enum {
    PWM_PIO_FIFO_PUT = 0, /**< FIFO has room; write the word. */
    PWM_PIO_FIFO_WAIT,    /**< FIFO is full and the deadline has not expired. */
    PWM_PIO_FIFO_FAIL,    /**< FIFO stayed full until the deadline. */
} pwm_pio_fifo_action_t;

/** @brief Bound a PIO FIFO push so a wedged state machine fails the apply instead of stalling Core 1. */
static inline pwm_pio_fifo_action_t pwm_pio_fifo_put_action(bool fifo_full, bool timed_out) {
    if (!fifo_full) {
        return PWM_PIO_FIFO_PUT;
    }
    return timed_out ? PWM_PIO_FIFO_FAIL : PWM_PIO_FIFO_WAIT;
}

#endif
