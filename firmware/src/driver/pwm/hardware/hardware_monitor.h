/**
 * @file hardware_monitor.h
 * @brief Hardware PWM-slice input monitor for the logical `pwm` layer.
 *
 * Measures the bank-A slice-B pins with the PWM slice counters in input mode and reports
 * frequency and duty using the existing `pwm_driver_state_t` shape.
 */

#ifndef PWMDRIVER_HW_MONITOR_H
#define PWMDRIVER_HW_MONITOR_H

#include "../pwm_driver.h"

/** @brief Claim the 8 slice-B input pins and start their measurement windows; repeated calls are ignored. */
bool hw_mon_init(void);

/**
 * @brief Poll one channel's windows and return its latest completed measurement.
 * @param channel Bank-local channel index.
 * @param state Caller-owned destination for the latest sample.
 * @return `false` before the first completed edge and high window pair, otherwise `true`.
 * @note Core 1 only. Each channel alternates a 250 ms rising-edge window (frequency)
 *       and a 250 ms high-time window (duty), so a sample refreshes about every 500 ms and the
 *       monitor is reliable above about 20 Hz. A static input reports `freq_hz = 0` with duty
 *       `0` or `100` from the sampled GPIO level.
 */
bool hw_mon_get(uint channel, pwm_driver_state_t *state);

#endif
