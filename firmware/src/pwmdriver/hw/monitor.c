/**
 * @file monitor.c
 * @brief Standalone Core 1 hardware-pin PWM monitor backend for the logical `pwmdriver` layer.
 *
 * This module measures the same GPIO bank used by the hardware PWM generator backend, but it
 * is intentionally not wired into `pwm_driver.c` yet. Each backend-local channel timestamps
 * GPIO edges in software and reconstructs one PWM sample from a high width plus the next full
 * period.
 *
 * Current limitations:
 * - This monitor uses one software GPIO interrupt per edge plus microsecond timestamps, so it
 *   is a low-frequency, best-effort monitor rather than a cycle-accurate or high-rate one.
 *   Treat it as suitable for slow signals only; use the PIO monitor for serious higher-rate
 *   measurement.
 * - It keeps only the latest exported sample for each channel.
 * - Inputs slower than 1 Hz are intentionally treated as permanent high or low levels rather
 *   than as in-spec PWM measurements.
 * - `pulse_count` increments once per completed observed period, so it reflects accepted
 *   edge-reconstructed cycles rather than hardware-captured edges.
 */

#include "monitor.h"

#include "pico/time.h"

#include "hardware/gpio.h"

#include "../monitor_gpio_common.h"
#include "../pwm_driver_internal.h"

/** @brief Inactivity threshold used to treat one channel as a permanent level. */
#define HW_MON_STATIC_TIMEOUT_US 1000000u
/* ponytail: This lookup table is sized to the current Pico GPIO count assumption.
 * The ceiling is platform drift if a future target exposes a different GPIO range. That is
 * acceptable now because the current RP2040/RP2350 Pico pin bank used by this firmware fits
 * within this bound. If board support widens later, replace this with an SDK- or board-level
 * GPIO-limit constant.
 */
/** @brief GPIO count used for the direct GPIO-to-channel lookup table. */
#define HW_MON_GPIO_COUNT 30u

/** @brief Runtime ownership and latest sample state for one hardware monitor channel. */
typedef pwm_gpio_mon_channel_t hw_mon_channel_t;

/** @brief Per-channel standalone hardware monitor runtime ownership table. */
static hw_mon_channel_t hw_mon_channels[HW_PWM_COUNT];
/** @brief Direct GPIO-to-channel lookup table; `-1` marks unrelated GPIOs. */
static int8_t hw_mon_gpio_to_channel[HW_MON_GPIO_COUNT] = {
    [0 ... HW_MON_GPIO_COUNT - 1] = -1
};
/** @brief Guards the standalone hardware monitor lifecycle so init only runs once. */
static bool hw_mon_initialized = false;

/** @brief Clear one channel's cached monitor sample and transient decode state. */
static void hw_mon_reset_channel(hw_mon_channel_t *ctx) {
    pwm_gpio_mon_reset_channel(ctx);
}

/** @brief Publish the defined unstable-read sentinel state. */
static void hw_mon_publish_unstable(hw_mon_channel_t *ctx) {
    /* ponytail: The unstable sentinel is retained to match the PIO monitor contract.
     * The ceiling is that this simpler hardware monitor could arguably use validity alone.
     * That tradeoff is acceptable now because cross-backend monitor consistency is more useful
     * than shaving one publication state. If the monitor APIs diverge later, revisit this.
     */
    pwm_gpio_mon_publish_state(ctx, HW_MON_UNSTABLE_FREQ_HZ, HW_MON_UNSTABLE_DUTY, false);
}

/** @brief Publish one captured period plus high width as exported frequency and duty. */
static void hw_mon_publish_sample(hw_mon_channel_t *ctx, uint64_t period_us, uint32_t high_us) {
    pwm_gpio_mon_publish_sample(ctx, period_us, high_us, HW_MON_UNSTABLE_FREQ_HZ, HW_MON_UNSTABLE_DUTY);
}

/** @brief Shared GPIO edge callback that timestamps hardware-monitor pin transitions. */
static void hw_mon_gpio_irq(uint gpio, uint32_t events) {
    pwm_gpio_mon_handle_irq(gpio, events, HW_MON_GPIO_COUNT, hw_mon_gpio_to_channel, hw_mon_channels, HW_MON_UNSTABLE_FREQ_HZ, HW_MON_UNSTABLE_DUTY);
}

/** @brief Read one channel state under interrupt exclusion and apply static-level fallback when idle. */
static bool hw_mon_read_channel(uint channel, pwm_driver_state_t *state) {
    return pwm_gpio_mon_read_channel(channel, state, hw_mon_channels, HW_MON_STATIC_TIMEOUT_US, PWM_HW_GPIO_PINS);
}

/** @copydoc hw_mon_init */
void hw_mon_init(void) {
    if (hw_mon_initialized) {
        return;
    }

    for (uint channel = 0; channel < HW_PWM_COUNT; channel++) {
        uint pin = PWM_HW_GPIO_PINS[channel];

        pwm_gpio_mon_init_pin(pin);

        hw_mon_gpio_to_channel[pin] = (int8_t)channel;
        hw_mon_reset_channel(&hw_mon_channels[channel]);

        /* ponytail: The monitor uses the SDK's one global GPIO callback. The ceiling is that
         * another module cannot install a different callback without coordination. That is
         * acceptable now because this firmware does not use GPIO callbacks anywhere else.
         * If that changes later, move to a shared callback dispatcher.
         */
        if (channel != 0u) {
            gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
        }
    }

    gpio_set_irq_enabled_with_callback(
        PWM_HW_GPIO_PINS[0],
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
        true,
        &hw_mon_gpio_irq);

    hw_mon_initialized = true;
}

/** @copydoc hw_mon_get */
bool hw_mon_get(uint channel, pwm_driver_state_t *state) {
    if (!hw_mon_initialized || channel >= HW_PWM_COUNT || state == NULL) {
        return false;
    }

    return hw_mon_read_channel(channel, state);
}