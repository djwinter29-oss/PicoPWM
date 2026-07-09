/**
 * @file pwm_driver.h
 * @brief Public logical-channel and multicore PWM control interfaces.
 */

#ifndef PWMDRIVER_PWM_DRIVER_H
#define PWMDRIVER_PWM_DRIVER_H

#include "pico/stdlib.h"

/** @brief Logical hardware PWM channel count. */
#define HW_PWM_COUNT 8
/** @brief Logical PIO PWM channel count. */
#define PIO_PWM_DRIVER_COUNT 8
/** @brief Logical software PWM channel count. */
#define SW_PWM_COUNT 8

/** @brief Logical base index for hardware PWM channels. */
#define HW_PWM_CHANNEL_BASE 0
/** @brief Logical base index for PIO PWM channels. */
#define PIO_PWM_CHANNEL_BASE (HW_PWM_CHANNEL_BASE + HW_PWM_COUNT)
/** @brief Logical base index for software PWM channels. */
#define SW_PWM_CHANNEL_BASE (PIO_PWM_CHANNEL_BASE + PIO_PWM_DRIVER_COUNT)
/** @brief Total logical PWM channel count across all backends. */
#define PWM_DRIVER_CHANNEL_COUNT (HW_PWM_COUNT + PIO_PWM_DRIVER_COUNT + SW_PWM_COUNT)

/** @brief Result codes returned by shared PWM control operations. */
typedef enum {
    PWM_DRIVER_RESULT_OK = 0, /**< The request completed successfully. */
    PWM_DRIVER_RESULT_BUSY, /**< Another command was already pending or executing. */
    PWM_DRIVER_RESULT_INVALID, /**< The caller supplied an invalid channel or value. */
    PWM_DRIVER_RESULT_UNAVAILABLE, /**< The requested operation is not available in the current context. */
    PWM_DRIVER_RESULT_TIMEOUT, /**< Core 1 did not publish a reply before the command timeout. */
    PWM_DRIVER_RESULT_APPLY_FAILED, /**< The backend rejected the admitted request. */
} pwm_driver_result_t;

/** @brief Realized logical state snapshot for one PWM channel. */
typedef struct {
    float freq_hz; /**< Realized output frequency in Hz. */
    float duty; /**< Realized duty cycle in the normalized range `[0.0, 1.0]`. */
    uint32_t pulse_count; /**< Monotonic generated-period count from power-on. */
} pwm_driver_state_t;

/**
 * @brief Apply both frequency and duty to one logical channel.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in the normalized range `[0.0, 1.0]`.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t control_set(uint channel, float freq_hz, float duty);

/**
 * @brief Apply frequency while preserving the currently realized duty.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t control_set_freq(uint channel, float freq_hz);

/**
 * @brief Apply duty while preserving the currently realized frequency.
 * @param channel Logical channel index.
 * @param duty Requested duty in the normalized range `[0.0, 1.0]`.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t control_set_duty(uint channel, float duty);

/**
 * @brief Read one logical channel snapshot.
 * @param channel Logical channel index.
 * @param state Caller-owned destination for the realized channel state.
 * @return `true` when the channel exists and the state was copied.
 */
bool control_get(uint channel, pwm_driver_state_t *state);

/**
 * @brief Read the realized frequency for one logical channel.
 * @param channel Logical channel index.
 * @return Realized frequency in Hz, or `0.0f` when the channel is invalid.
 */
float control_get_freq(uint channel);

/**
 * @brief Read the realized duty for one logical channel.
 * @param channel Logical channel index.
 * @return Realized duty in the normalized range `[0.0, 1.0]`, or `0.0f` when invalid.
 */
float control_get_duty(uint channel);

/**
 * @brief Read the monotonic pulse counter for one logical channel.
 * @param channel Logical channel index.
 * @return Realized pulse counter, or `0` when the channel is invalid.
 */
uint32_t control_get_pulse_count(uint channel);

/**
 * @brief Return whether one logical channel is currently enabled.
 * @param channel Logical channel index.
 * @return `true` when the realized frequency is greater than `0`.
 */
bool control_is_enabled(uint channel);

/**
 * @brief Stop all channels and restore the shared default state.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t control_stop_all(void);

/**
 * @brief Launch Core 1 backend ownership and start the PWM driver runtime.
 */
void pwm_driver_launch(void);

/**
 * @brief Return whether Core 1 finished backend initialization.
 * @return `true` once the PWM driver runtime is ready to accept commands.
 */
bool pwm_driver_is_ready(void);

/**
 * @brief Submit one cross-core logical channel update.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in the normalized range `[0.0, 1.0]`.
 * @return Result code for the admitted command attempt.
 * @note This is a Core 0 command-ingress API. Higher command layers should normally
 *       prefer the `control_*()` helpers for full-state or read-modify-write updates.
 */
pwm_driver_result_t pwm_driver_set_freq(uint channel, float freq_hz, float duty);

/**
 * @brief Read the latest published realized state for one logical channel.
 * @param channel Logical channel index.
 * @param state Caller-owned destination for the realized channel state.
 * @return `true` when the channel exists and the state was copied.
 */
bool pwm_driver_get(uint channel, pwm_driver_state_t *state);

#endif