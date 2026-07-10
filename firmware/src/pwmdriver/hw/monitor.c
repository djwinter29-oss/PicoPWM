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
 * - This monitor uses microsecond timestamps from GPIO edge IRQs, so it is approximate rather
 *   than cycle-accurate.
 * - It keeps only the latest exported sample for each channel.
 * - Inputs slower than 1 Hz are intentionally treated as permanent high or low levels rather
 *   than as in-spec PWM measurements.
 * - The backend does not provide a reliable received pulse count and always reports
 *   `pulse_count = 0`.
 */

#include "monitor.h"

#include "pico/time.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"

#include "../pwm_driver_internal.h"

/** @brief Inactivity threshold used to treat one channel as a permanent level. */
#define HW_MON_STATIC_TIMEOUT_US 1000000u

/** @brief Runtime ownership and latest sample state for one hardware monitor channel. */
typedef struct {
    pwm_driver_state_t state; /**< Latest exported monitor state. */
    uint64_t last_edge_us; /**< Timestamp of the most recent observed GPIO transition. */
    uint64_t last_rise_us; /**< Timestamp of the most recent observed rising edge. */
    uint32_t high_us; /**< Cached high duration captured after the most recent rising edge. */
    bool sample_valid; /**< Indicates whether one full PWM sample has been captured. */
    bool have_rise; /**< Indicates whether the monitor has observed at least one rising edge. */
    bool have_high; /**< Indicates whether the monitor has captured a high width for the active cycle. */
} hw_mon_channel_t;

/** @brief Per-channel standalone hardware monitor runtime ownership table. */
static hw_mon_channel_t hw_mon_channels[HW_PWM_COUNT];
/** @brief Guards the standalone hardware monitor lifecycle so init only runs once. */
static bool hw_mon_initialized = false;

/** @brief Publish one exported monitor state and matching validity flag. */
static void hw_mon_publish_state(hw_mon_channel_t *ctx, uint32_t freq_hz, uint8_t duty, bool sample_valid) {
    ctx->state.freq_hz = freq_hz;
    ctx->state.duty = duty;
    ctx->state.pulse_count = 0u;
    ctx->sample_valid = sample_valid;
}

/** @brief Clear one channel's cached monitor sample and transient decode state. */
static void hw_mon_reset_channel(hw_mon_channel_t *ctx) {
    uint64_t now_us = time_us_64();

    hw_mon_publish_state(ctx, 0u, 0u, false);
    ctx->last_edge_us = now_us;
    ctx->last_rise_us = now_us;
    ctx->high_us = 0u;
    ctx->have_rise = false;
    ctx->have_high = false;
}

/** @brief Publish the defined unstable-read sentinel state. */
static void hw_mon_publish_unstable(hw_mon_channel_t *ctx) {
    hw_mon_publish_state(ctx, HW_MON_UNSTABLE_FREQ_HZ, HW_MON_UNSTABLE_DUTY, false);
}

/** @brief Publish one captured period plus high width as exported frequency and duty. */
static void hw_mon_publish_sample(hw_mon_channel_t *ctx, uint64_t period_us, uint32_t high_us) {
    uint64_t rounded_freq_hz;
    uint32_t duty_percent;

    if (period_us == 0u || high_us > period_us) {
        hw_mon_publish_unstable(ctx);
        return;
    }

    rounded_freq_hz = (1000000u + (period_us / 2u)) / period_us;
    if (rounded_freq_hz > UINT32_MAX) {
        rounded_freq_hz = UINT32_MAX;
    }

    duty_percent = (uint32_t)(((uint64_t)high_us * 100u + (period_us / 2u)) / period_us);
    if (duty_percent > 100u) {
        duty_percent = 100u;
    }

    hw_mon_publish_state(ctx, (uint32_t)rounded_freq_hz, (uint8_t)duty_percent, true);
}

/** @brief Return the backend-local channel index for one hardware monitor GPIO, or -1 when unrelated. */
static int hw_mon_channel_for_gpio(uint gpio) {
    for (uint channel = 0; channel < HW_PWM_COUNT; channel++) {
        if (PWM_HW_GPIO_PINS[channel] == gpio) {
            return (int)channel;
        }
    }

    return -1;
}

/** @brief Shared GPIO edge callback that timestamps hardware-monitor pin transitions. */
static void hw_mon_gpio_irq(uint gpio, uint32_t events) {
    int channel = hw_mon_channel_for_gpio(gpio);
    uint64_t now_us;
    hw_mon_channel_t *ctx;

    if (channel < 0) {
        return;
    }

    now_us = time_us_64();
    ctx = &hw_mon_channels[channel];
    ctx->last_edge_us = now_us;

    if ((events & GPIO_IRQ_EDGE_RISE) != 0u) {
        if (ctx->have_rise && ctx->have_high) {
            hw_mon_publish_sample(ctx, now_us - ctx->last_rise_us, ctx->high_us);
        }

        ctx->last_rise_us = now_us;
        ctx->have_rise = true;
        ctx->have_high = false;
    }

    if ((events & GPIO_IRQ_EDGE_FALL) != 0u && ctx->have_rise && now_us >= ctx->last_rise_us) {
        uint64_t high_us = now_us - ctx->last_rise_us;

        if (high_us > UINT32_MAX) {
            high_us = UINT32_MAX;
        }

        ctx->high_us = (uint32_t)high_us;
        ctx->have_high = true;
    }
}

/** @brief Read one channel state under interrupt exclusion and apply static-level fallback when idle. */
static bool hw_mon_read_channel(uint channel, pwm_driver_state_t *state) {
    uint32_t irq_state;
    pwm_driver_state_t snapshot;
    uint64_t last_edge_us;
    bool sample_valid;
    uint64_t now_us;

    irq_state = save_and_disable_interrupts();
    snapshot = hw_mon_channels[channel].state;
    last_edge_us = hw_mon_channels[channel].last_edge_us;
    sample_valid = hw_mon_channels[channel].sample_valid;
    restore_interrupts(irq_state);

    now_us = time_us_64();
    if ((now_us - last_edge_us) >= HW_MON_STATIC_TIMEOUT_US) {
        state->freq_hz = 0u;
        state->duty = gpio_get(PWM_HW_GPIO_PINS[channel]) ? 100u : 0u;
        state->pulse_count = 0u;
        return true;
    }

    *state = snapshot;
    return sample_valid;
}

/** @copydoc hw_mon_init */
void hw_mon_init(void) {
    if (hw_mon_initialized) {
        return;
    }

    for (uint channel = 0; channel < HW_PWM_COUNT; channel++) {
        uint pin = PWM_HW_GPIO_PINS[channel];

        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        hw_mon_reset_channel(&hw_mon_channels[channel]);

        /* ponytail: The monitor uses the SDK's one global GPIO callback. The ceiling is that
         * another module cannot install a different callback without coordination. That is
         * acceptable now because this firmware does not use GPIO callbacks anywhere else.
         * If that changes later, move to a shared callback dispatcher.
         */
        if (channel == 0u) {
            gpio_set_irq_enabled_with_callback(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &hw_mon_gpio_irq);
        } else {
            gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
        }
    }

    hw_mon_initialized = true;
}

/** @copydoc hw_mon_get */
bool hw_mon_get(uint channel, pwm_driver_state_t *state) {
    if (!hw_mon_initialized || channel >= HW_PWM_COUNT || state == NULL) {
        return false;
    }

    return hw_mon_read_channel(channel, state);
}