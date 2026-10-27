/**
 * @file monitor.c
 * @brief Standalone Core 1 software-PWM pin monitor backend for the logical `pwm` layer.
 *
 * This module measures the configured software monitor GPIO bank. Each backend-local channel timestamps
 * GPIO edges in software and reconstructs one PWM sample from a high width plus the next full
 * period.
 *
 * Current limitations:
 * - This monitor uses one software GPIO interrupt per edge plus microsecond timestamps, so it
 *   is a low-frequency, best-effort monitor rather than a cycle-accurate one.
 * - It keeps only the latest exported sample for each channel.
 * - Inputs slower than 1 Hz are intentionally treated as permanent high or low levels rather
 *   than as in-spec PWM measurements.
 */

#include "software_monitor.h"

#include "pico/time.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"

#include "../pwm_driver_internal.h"

/** @brief Inactivity threshold used to treat one channel as a permanent level. */
#define SW_MON_STATIC_TIMEOUT_US 1000000u
/* ponytail: sized to the RP2040/RP2350 Pico GPIO count; replace with an SDK constant if a board exposes more pins. */
/** @brief GPIO count used for the direct GPIO-to-channel lookup table. */
#define SW_MON_GPIO_COUNT 30u

/** @brief Runtime state of one software-monitored channel. */
typedef struct {
    pwm_driver_state_t state; /**< Latest exported sample. */
    bool sample_valid;        /**< Whether one full sample has been captured. */
    uint64_t last_edge_us;    /**< Timestamp of the latest edge, used for the static-level timeout. */
    uint64_t last_rise_us;    /**< Timestamp of the latest rising edge. */
    uint32_t high_us;         /**< High width captured after the latest rising edge. */
    bool have_rise;           /**< A rising edge has been seen. */
    bool have_high;           /**< A high width is captured for the current cycle. */
} sw_mon_channel_t;

/** @brief Per-channel monitor runtime state, indexed by bank-local channel. */
static sw_mon_channel_t sw_mon_channels[PWM_DRIVER_CONFIG_BANK_SIZE];
/** @brief Direct GPIO-to-channel lookup table; `-1` marks unrelated GPIOs. */
static int8_t sw_mon_gpio_to_channel[SW_MON_GPIO_COUNT] = {[0 ... SW_MON_GPIO_COUNT - 1] = -1};
/** @brief Guards the monitor lifecycle so init only runs once. */
static bool sw_mon_initialized = false;

/** @brief Publish one exported state and its validity flag. */
static void sw_mon_publish(sw_mon_channel_t *ctx, uint32_t freq_hz, uint8_t duty, bool valid) {
    ctx->state.freq_hz = freq_hz;
    ctx->state.duty = duty;
    ctx->sample_valid = valid;
}

/** @brief Convert one captured period and high width into frequency and duty, or mark the sample unstable. */
static void sw_mon_publish_sample(sw_mon_channel_t *ctx, uint64_t period_us, uint32_t high_us) {
    if (period_us == 0u || high_us > period_us) {
        sw_mon_publish(ctx, SW_MON_UNSTABLE_FREQ_HZ, SW_MON_UNSTABLE_DUTY, false);
        return;
    }

    uint64_t freq_hz = (1000000u + (period_us / 2u)) / period_us;
    uint32_t duty = (uint32_t)(((uint64_t)high_us * 100u + (period_us / 2u)) / period_us);

    sw_mon_publish(ctx, freq_hz > UINT32_MAX ? UINT32_MAX : (uint32_t)freq_hz, (uint8_t)(duty > 100u ? 100u : duty),
                   true);
}

/** @copydoc sw_mon_handle_gpio_irq */
void sw_mon_handle_gpio_irq(uint gpio, uint32_t events) {
    if (gpio >= SW_MON_GPIO_COUNT || sw_mon_gpio_to_channel[gpio] < 0) {
        return;
    }

    sw_mon_channel_t *ctx = &sw_mon_channels[sw_mon_gpio_to_channel[gpio]];
    uint64_t now_us = time_us_64();

    ctx->last_edge_us = now_us;

    if ((events & GPIO_IRQ_EDGE_RISE) != 0u) {
        if (ctx->have_rise && ctx->have_high) {
            sw_mon_publish_sample(ctx, now_us - ctx->last_rise_us, ctx->high_us);
        }
        ctx->last_rise_us = now_us;
        ctx->have_rise = true;
        ctx->have_high = false;
    }

    if ((events & GPIO_IRQ_EDGE_FALL) != 0u && ctx->have_rise && now_us >= ctx->last_rise_us) {
        uint64_t high_us = now_us - ctx->last_rise_us;

        ctx->high_us = high_us > UINT32_MAX ? UINT32_MAX : (uint32_t)high_us;
        ctx->have_high = true;
    }
}

/** @copydoc sw_mon_init */
bool sw_mon_init(void) {
    if (sw_mon_initialized) {
        return true;
    }

    for (uint channel = 0; channel < PWM_DRIVER_CONFIG_BANK_SIZE; channel++) {
        uint pin = software_pwm_config.channels[channel].gpio;
        uint64_t now_us = time_us_64();

        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        sw_mon_gpio_to_channel[pin] = (int8_t)channel;
        sw_mon_channels[channel] = (sw_mon_channel_t){.last_edge_us = now_us, .last_rise_us = now_us};
        gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    }

    sw_mon_initialized = true;
    return true;
}

/** @copydoc sw_mon_get */
bool sw_mon_get(uint channel, pwm_driver_state_t *state) {
    if (!sw_mon_initialized || channel >= PWM_DRIVER_CONFIG_BANK_SIZE || state == NULL) {
        return false;
    }

    uint32_t irq_state = save_and_disable_interrupts();
    sw_mon_channel_t ctx = sw_mon_channels[channel];

    restore_interrupts(irq_state);

    if ((time_us_64() - ctx.last_edge_us) >= SW_MON_STATIC_TIMEOUT_US) {
        state->freq_hz = 0u;
        state->duty = gpio_get(software_pwm_config.channels[channel].gpio) ? 100u : 0u;
        return true;
    }

    *state = ctx.state;
    return ctx.sample_valid;
}
