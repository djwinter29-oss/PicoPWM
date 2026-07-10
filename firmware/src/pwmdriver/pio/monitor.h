/**
 * @file monitor.h
 * @brief Standalone Core 1 PIO PWM monitor backend prototype for the logical `pwmdriver` layer.
 *
 * This module is intentionally not integrated into `pwm_driver.c` yet. It provides a
 * PIO-based monitor implementation that measures the PIO logical channel pin bank and
 * reports approximate frequency and duty cycle using the existing `pwm_driver_state_t`
 * shape.
 */

#ifndef PWMDRIVER_PIO_MONITOR_H
#define PWMDRIVER_PIO_MONITOR_H

#include "../pwm_driver.h"

/** @brief Sentinel frequency returned when repeated reads do not settle to a close sample. */
#define PIO_MON_UNSTABLE_FREQ_HZ 0x0fffffffu
/** @brief Sentinel duty returned when repeated reads do not settle to a close sample. */
#define PIO_MON_UNSTABLE_DUTY 0u
/** @brief Return whether one exported monitor state represents the unstable-read sentinel. */
#define PIO_MON_IS_UNSTABLE(state_value) \
	((state_value).freq_hz == PIO_MON_UNSTABLE_FREQ_HZ && (state_value).duty == PIO_MON_UNSTABLE_DUTY)

/** @brief Initialize the standalone PIO monitor module and arm all PIO-bank input pins.
 *
 * Repeated calls are ignored after the first successful initialization.
 */
void pio_mon_init(void);

/**
 * @brief Read the latest exported monitor state for one backend-local channel.
 * @param channel Backend-local PIO monitor channel index.
 * @param state Caller-owned destination for the latest exported state.
 * @return `true` when the returned state is a stable PWM measurement or a stable static level.
 *         Returns `false` before the first stable sample and when the exported state is the
 *         unstable-read sentinel.
 * @note This standalone prototype keeps only the latest DMA-written high/low sample pair,
 *       so intermediate PWM periods are intentionally discarded when callers read slowly.
 * @note This backend does not provide a reliable received pulse count. It always reports
 *       `pulse_count = 0`.
 * @note The DMA drain is intentionally a finite long-running transfer rather than a permanent
 *       self-rearming stream. With the current transfer count, monitoring eventually stops
 *       updating after roughly 36 minutes at 1 MHz input. Because initialization is one-shot,
 *       that exhausted state persists until reboot. After that point the backend preserves the
 *       last exported state and no longer applies the one-second static-level fallback.
 * @note Reads retry up to five times and accept the sample when two decoded results are close
 *       in both frequency and duty. Frequency closeness uses a `max(1 Hz, 1%)` rule and duty
 *       closeness uses `1%`. If the sample keeps moving, the monitor returns the
 *       defined sentinel state `freq_hz = PIO_MON_UNSTABLE_FREQ_HZ`,
 *       `duty = PIO_MON_UNSTABLE_DUTY`.
 * @note The snapshot acceptance path is still best-effort rather than strictly coherent, but
 *       the residual risk is intentionally treated as negligible for this prototype because
 *       callers require two similar decoded reads before accepting a sample.
 * @note This module is read-driven. Unstable-read handling and the one-second static-level
 *       timeout are serviced when callers invoke @ref pio_mon_get.
 * @note If no input transition is observed for more than one second, the monitor treats the
 *       signal as out of spec for PWM and returns `freq_hz = 0` with `duty = 0` or `100`
 *       based on the sampled GPIO level.
 */
bool pio_mon_get(uint channel, pwm_driver_state_t *state);

#endif