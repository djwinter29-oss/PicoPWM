/**
 * @file monitor.h
 * @brief Standalone Core 1 PIO PWM monitor backend prototype for the logical `pwmdriver` layer.
 *
 * This module provides a PIO-based monitor implementation that measures the configured
 * PIO monitor channel bank and reports approximate frequency and duty cycle using the
 * existing `pwm_driver_state_t` shape.
 */

#ifndef PWMDRIVER_PIO_MONITOR_H
#define PWMDRIVER_PIO_MONITOR_H

#include "../pwm_driver.h"

/** @brief Sentinel frequency returned when a captured pair is invalid. */
#define PIO_MON_UNSTABLE_FREQ_HZ 0x0fffffffu
/** @brief Sentinel duty returned when a captured pair is invalid. */
#define PIO_MON_UNSTABLE_DUTY 0u
/** @brief Return whether one exported monitor state represents the unstable-read sentinel. */
#define PIO_MON_IS_UNSTABLE(state_value) \
	((state_value).freq_hz == PIO_MON_UNSTABLE_FREQ_HZ && (state_value).duty == PIO_MON_UNSTABLE_DUTY)

/** @brief Initialize the standalone PIO monitor module and arm all PIO-bank input pins.
 *
 * Repeated calls are ignored after the first successful initialization.
 */
bool pio_mon_init(void);

/**
 * @brief Capture and read one exported monitor state for one backend-local channel.
 * @param channel Backend-local PIO monitor channel index.
 * @param state Caller-owned destination for the latest exported state.
 * @return `true` when the returned state is a stable PWM measurement or a stable static level.
 *         Returns `false` before the first stable sample and when the exported state is the
 *         unstable-read sentinel.
 * @note Each read starts one PIO capture and consumes one high/low pair directly from the
 *       RX FIFO; waveform history and intermediate periods are intentionally discarded.
 * @note Invalid pairs return the sentinel `freq_hz = PIO_MON_UNSTABLE_FREQ_HZ`,
 *       `duty = PIO_MON_UNSTABLE_DUTY`.
 * @note If no input transition is observed for more than one second, the monitor reports a
 *       static level as `freq_hz = 0` with `duty = 0` or `100` based on the sampled GPIO.
 * @note `pulse_count` is always `0`.
 */
bool pio_mon_get(uint channel, pwm_driver_state_t *state);

#endif