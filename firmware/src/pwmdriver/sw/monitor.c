/**
 * @file monitor.c
 * @brief Standalone Core 1 software-PWM pin monitor backend for the logical `pwmdriver` layer.
 *
 * This module measures the same GPIO bank used by the software PWM generator backend, but it
 * is intentionally not wired into `pwm_driver.c` yet. Each backend-local channel timestamps
 * GPIO edges in software and reconstructs one PWM sample from a high width plus the next full
 * period.
 *
 * Current limitations:
 * - This monitor uses one software GPIO interrupt per edge plus microsecond timestamps, so it
 *   is a low-frequency, best-effort monitor rather than a cycle-accurate one.
 * - It keeps only the latest exported sample for each channel.
 * - Inputs slower than 1 Hz are intentionally treated as permanent high or low levels rather
 *   than as in-spec PWM measurements.
 * - `pulse_count` increments once per completed observed period, so it reflects accepted
 *   edge-reconstructed cycles rather than hardware-captured edges.
 *
 * ponytail: This module intentionally mirrors the small edge-timestamp pattern used by the
 * hardware monitor backend instead of introducing a shared private abstraction immediately.
 * The ceiling is code drift between the two monitor implementations if both keep evolving.
 * That tradeoff is acceptable now because each backend still wants its own pin-bank constants,
 * API names, and limitations called out locally. If the two monitors continue to evolve in
 * parallel, extract a private shared helper for the common edge-capture and idle-timeout logic.
 */

#include "monitor.h"

#include "pico/time.h"

#include "hardware/gpio.h"

#include "../monitor_gpio_common.h"
#include "../pwm_driver_internal.h"

/** @brief Inactivity threshold used to treat one channel as a permanent level. */
#define SW_MON_STATIC_TIMEOUT_US 1000000u
/* ponytail: This lookup table is sized to the current Pico GPIO count assumption.
 * The ceiling is platform drift if a future target exposes a different GPIO range. That is
 * acceptable now because the current RP2040/RP2350 Pico pin bank used by this firmware fits
 * within this bound. If board support widens later, replace this with an SDK- or board-level
 * GPIO-limit constant.
 */
/** @brief GPIO count used for the direct GPIO-to-channel lookup table. */
#define SW_MON_GPIO_COUNT 30u

/** @brief Per-channel standalone software monitor runtime ownership table. */
static pwm_gpio_mon_channel_t sw_mon_channels[SW_PWM_COUNT];
/** @brief Direct GPIO-to-channel lookup table; `-1` marks unrelated GPIOs. */
static int8_t sw_mon_gpio_to_channel[SW_MON_GPIO_COUNT] = {
    [0 ... SW_MON_GPIO_COUNT - 1] = -1
};
/** @brief Guards the standalone software monitor lifecycle so init only runs once. */
static bool sw_mon_initialized = false;

/** @brief Shared GPIO edge callback that timestamps software-monitor pin transitions. */
static void sw_mon_gpio_irq(uint gpio, uint32_t events) {
    pwm_gpio_mon_handle_irq(gpio, events, SW_MON_GPIO_COUNT, sw_mon_gpio_to_channel, sw_mon_channels, SW_MON_UNSTABLE_FREQ_HZ, SW_MON_UNSTABLE_DUTY);
}

/** @brief Read one channel state under interrupt exclusion and apply static-level fallback when idle. */
static bool sw_mon_read_channel(uint channel, pwm_driver_state_t *state) {
    return pwm_gpio_mon_read_channel(channel, state, sw_mon_channels, SW_MON_STATIC_TIMEOUT_US, PWM_SW_GPIO_PINS);
}

/** @copydoc sw_mon_init */
void sw_mon_init(void) {
    if (sw_mon_initialized) {
        return;
    }

    for (uint channel = 0; channel < SW_PWM_COUNT; channel++) {
        uint pin = PWM_SW_GPIO_PINS[channel];

        pwm_gpio_mon_init_pin(pin);
        sw_mon_gpio_to_channel[pin] = (int8_t)channel;
        pwm_gpio_mon_reset_channel(&sw_mon_channels[channel]);

        if (channel != 0u) {
            gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
        }
    }

    gpio_set_irq_enabled_with_callback(
        PWM_SW_GPIO_PINS[0],
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
        true,
        &sw_mon_gpio_irq);

    sw_mon_initialized = true;
}

/** @copydoc sw_mon_get */
bool sw_mon_get(uint channel, pwm_driver_state_t *state) {
    if (!sw_mon_initialized || channel >= SW_PWM_COUNT || state == NULL) {
        return false;
    }

    return sw_mon_read_channel(channel, state);
}