/**
 * @file monitor.h
 * @brief Standalone Core 1 software-PWM pin monitor backend for the logical `pwmdriver` layer.
 *
 * This module is intentionally not integrated into `pwm_driver.c` yet. It measures the
 * software PWM logical channel pin bank and reports approximate frequency and duty cycle
 * using the existing `pwm_driver_state_t` shape.
 *
 * This monitor is intentionally limited to low-frequency, best-effort observation. It uses
 * one software GPIO interrupt per edge plus microsecond timestamps. That makes it a much
 * better fit for the software PWM bank than for the hardware or PIO banks, but it is still
 * a best-effort software measurement path rather than a cycle-accurate monitor. Treat it as
 * a monitor for the slow software PWM range, roughly `1 Hz .. 1 kHz`, not as a general
 * purpose high-rate monitor.
 *
 * This module reuses the same GPIO bank as the software PWM generator backend and is intended
 * as a standalone monitor prototype. It must not be assumed safe to run alongside the
 * generator without an explicit integration design that resolves shared GPIO ownership.
 */

#ifndef PWMDRIVER_SW_MONITOR_H
#define PWMDRIVER_SW_MONITOR_H

#include "../pwm_driver.h"

/** @brief Sentinel frequency returned when the software monitor cannot publish a sane sample. */
#define SW_MON_UNSTABLE_FREQ_HZ 0x0fffffffu
/** @brief Sentinel duty returned when the software monitor cannot publish a sane sample. */
#define SW_MON_UNSTABLE_DUTY 0u
/** @brief Return whether one exported software-monitor state is the unstable sentinel. */
#define SW_MON_IS_UNSTABLE(state_value) \
	((state_value).freq_hz == SW_MON_UNSTABLE_FREQ_HZ && (state_value).duty == SW_MON_UNSTABLE_DUTY)

/** @brief Initialize the standalone software monitor module and arm all software-channel input pins.
 *
 * Repeated calls are ignored after the first successful initialization.
 */
void sw_mon_init(void);

/**
 * @brief Read the latest exported monitor state for one backend-local software channel.
 * @param channel Backend-local software monitor channel index.
 * @param state Caller-owned destination for the latest exported state.
 * @return `true` when the returned state is a stable PWM measurement or a stable static level.
 *         Returns `false` before the first stable sample and when the exported state is the
 *         unstable sentinel.
 * @note The monitor timestamps GPIO edges in microseconds and derives one period from
 *       consecutive rising edges plus one high width from the falling edge between them.
 * @note This is a low-frequency, best-effort monitor only.
 * @note If no input transition is observed for more than one second, the monitor reports a
 *       static level as `freq_hz = 0` with `duty = 0` or `100` based on the sampled GPIO.
 * @note `pulse_count` is always `0`.
 */
bool sw_mon_get(uint channel, pwm_driver_state_t *state);

#endif