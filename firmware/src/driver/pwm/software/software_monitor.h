// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

/**
 * @file monitor.h
 * @brief Standalone Core 1 software-PWM pin monitor backend for the logical `pwm` layer.
 *
 * This module measures the configured software monitor channel bank and reports approximate
 * frequency and duty cycle using the existing `pwm_state_t` shape.
 *
 * This monitor is intentionally limited to low-frequency, best-effort observation. It uses
 * one timer to sample all eight inputs plus microsecond timestamps. That makes it a much
 * better fit for the software PWM bank than for the hardware or PIO banks, but it is still
 * a best-effort software measurement path rather than a cycle-accurate monitor. Treat it as
 * a monitor for the slow software PWM range, roughly `1 Hz .. 1 kHz`.
 * Operation up to 10 kHz is experimental and requires hardware qualification.
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
#define SW_MON_IS_UNSTABLE(state_value)                                                                                \
    ((state_value).freq_hz == SW_MON_UNSTABLE_FREQ_HZ && (state_value).duty == SW_MON_UNSTABLE_DUTY)

/** @brief Initialize the standalone software monitor module and arm all software-channel input pins.
 *
 * Repeated calls are ignored after the first successful initialization.
 */
bool sw_mon_init(void);

/** @brief Lifetime scan-spacing diagnostics, not total CPU utilization or accuracy certification. */
typedef struct {
    uint32_t scans;         /**< Scan count, saturating at UINT32_MAX. */
    uint32_t min_gap_us;    /**< Minimum observed gap; UINT32_MAX until two scans occur. */
    uint32_t max_gap_us;    /**< Maximum observed gap, including servicing delays and flash stalls. */
    uint32_t gaps_over_5us; /**< Gaps exceeding the experimental 10 kHz timing budget; saturating count. */
} sw_mon_scan_timing_t;

/**
 * @brief Copy scan diagnostics on the backend-owning Core 1; not a cross-core interface.
 * @param timing Non-null destination for the diagnostic snapshot.
 */
void sw_mon_get_scan_timing(sw_mon_scan_timing_t *timing);

/**
 * @brief Read the latest exported monitor state for one backend-local software channel.
 * @param channel Backend-local software monitor channel index.
 * @param state Caller-owned destination for the latest exported state.
 * @return `true` when the returned state is a stable PWM measurement or a stable static level.
 *         Returns `false` before the first stable sample or when no scan has arrived for at least
 *         one second; the returned state is the unstable sentinel.
 * @note A private Core 1 timer samples all GPIOs with a 2 us completion-relative delay.
 *       Complete observed cycles accumulate over at least 20 ms before publication;
 *       slower signals publish after a complete cycle. Narrow pulses may be missed and
 *       out-of-range signals may alias to lower frequencies or static levels.
 * @note This is a low-frequency, best-effort monitor only.
 * @note If no input transition is observed for more than one second, the monitor reports a
 *       static level as `freq_hz = 0` with `duty = 0` or `100` based on the sampled GPIO.
 */
bool sw_mon_get(uint channel, pwm_state_t *state);

#endif