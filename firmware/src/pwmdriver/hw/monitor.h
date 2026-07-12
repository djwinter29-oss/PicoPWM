/**
 * @file monitor.h
 * @brief Standalone Core 1 hardware-pin PWM monitor backend for the logical `pwmdriver` layer.
 *
 * This module is intentionally not integrated into `pwm_driver.c` yet. It measures the
 * hardware PWM logical channel pin bank and reports approximate frequency and duty cycle
 * using the existing `pwm_driver_state_t` shape.
 *
 * This monitor is intentionally limited to low-frequency, best-effort observation. It uses
 * one software GPIO interrupt per edge plus microsecond timestamps, so it is not suitable
 * for high-rate PWM monitoring and should not be treated as a peer to the PIO monitor for
 * kHz-to-MHz measurement work.
 */

#ifndef PWMDRIVER_HW_MONITOR_H
#define PWMDRIVER_HW_MONITOR_H

#include "../pwm_driver.h"

/** @brief Sentinel frequency returned when the hardware monitor cannot publish a sane sample. */
#define HW_MON_UNSTABLE_FREQ_HZ 0x0fffffffu
/** @brief Sentinel duty returned when the hardware monitor cannot publish a sane sample. */
#define HW_MON_UNSTABLE_DUTY 0u
/** @brief Return whether one exported hardware-monitor state is the unstable sentinel. */
#define HW_MON_IS_UNSTABLE(state_value) \
	((state_value).freq_hz == HW_MON_UNSTABLE_FREQ_HZ && (state_value).duty == HW_MON_UNSTABLE_DUTY)

/** @brief Initialize the standalone hardware monitor module and arm all hardware-channel input pins.
 *
 * Repeated calls are ignored after the first successful initialization.
 */
void hw_mon_init(void);

/**
 * @brief Read the latest exported monitor state for one backend-local hardware channel.
 * @param channel Backend-local hardware monitor channel index.
 * @param state Caller-owned destination for the latest exported state.
 * @return `true` when the returned state is a stable PWM measurement or a stable static level.
 *         Returns `false` before the first stable sample and when the exported state is the
 *         unstable sentinel.
 * @note The monitor timestamps GPIO edges in microseconds and derives one period from
 *       consecutive rising edges plus one high width from the falling edge between them.
 * @note This is a low-frequency, best-effort monitor only. Use the PIO monitor for serious
 *       higher-rate PWM measurement.
 * @note If no input transition is observed for more than one second, the monitor reports a
 *       static level as `freq_hz = 0` with `duty = 0` or `100` based on the sampled GPIO.
 * @note `pulse_count` increments once per completed observed period reconstructed from
 *       rising/falling/rising edge sequences.
 */
bool hw_mon_get(uint channel, pwm_driver_state_t *state);

#endif