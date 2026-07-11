/**
 * @file monitor_gpio_common.h
 * @brief Private shared helpers for GPIO edge timestamp monitor backends.
 */

#ifndef PWMDRIVER_MONITOR_GPIO_COMMON_H
#define PWMDRIVER_MONITOR_GPIO_COMMON_H

#include "pwm_driver.h"

#include "pico/time.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"

/** @brief Shared transient edge-tracking state for one GPIO monitor channel. */
typedef struct {
    uint64_t last_edge_us; /**< Timestamp of the most recent observed GPIO transition. */
    uint64_t last_rise_us; /**< Timestamp of the most recent observed rising edge. */
    uint32_t high_us; /**< Cached high duration captured after the most recent rising edge. */
    bool have_rise; /**< Indicates whether the monitor has observed at least one rising edge. */
    bool have_high; /**< Indicates whether the monitor has captured a high width for the active cycle. */
} pwm_gpio_mon_capture_t;

/** @brief Shared runtime ownership and latest sample state for one GPIO monitor channel. */
typedef struct {
    pwm_driver_state_t state; /**< Latest exported monitor state. */
    bool sample_valid; /**< Indicates whether one full PWM sample has been captured. */
    pwm_gpio_mon_capture_t capture; /**< Transient edge timestamps and partial-sample tracking. */
} pwm_gpio_mon_channel_t;

/** @brief Publish one exported monitor state and matching validity flag. */
static inline void pwm_gpio_mon_publish_state(pwm_gpio_mon_channel_t *ctx, uint32_t freq_hz, uint8_t duty, bool sample_valid) {
    ctx->state.freq_hz = freq_hz;
    ctx->state.duty = duty;
    ctx->state.pulse_count = 0u;
    ctx->sample_valid = sample_valid;
}

/** @brief Configure one monitored GPIO as an input before IRQ arming. */
static inline void pwm_gpio_mon_init_pin(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
}

/** @brief Clear one channel's cached monitor sample and transient decode state. */
static inline void pwm_gpio_mon_reset_channel(pwm_gpio_mon_channel_t *ctx) {
    uint64_t now_us = time_us_64();

    pwm_gpio_mon_publish_state(ctx, 0u, 0u, false);
    ctx->capture.last_edge_us = now_us;
    ctx->capture.last_rise_us = now_us;
    ctx->capture.high_us = 0u;
    ctx->capture.have_rise = false;
    ctx->capture.have_high = false;
}

/** @brief Publish one captured period plus high width as exported frequency and duty. */
static inline void pwm_gpio_mon_publish_sample(pwm_gpio_mon_channel_t *ctx, uint64_t period_us, uint32_t high_us, uint32_t unstable_freq_hz, uint8_t unstable_duty) {
    uint64_t rounded_freq_hz;
    uint32_t duty_percent;

    if (period_us == 0u || high_us > period_us) {
        pwm_gpio_mon_publish_state(ctx, unstable_freq_hz, unstable_duty, false);
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

    pwm_gpio_mon_publish_state(ctx, (uint32_t)rounded_freq_hz, (uint8_t)duty_percent, true);
}

/** @brief Update one GPIO monitor bank from one observed edge event. */
static inline void pwm_gpio_mon_handle_irq(uint gpio, uint32_t events, uint gpio_count, const int8_t *gpio_to_channel, pwm_gpio_mon_channel_t *channels, uint32_t unstable_freq_hz, uint8_t unstable_duty) {
    int channel;
    uint64_t now_us;
    pwm_gpio_mon_channel_t *ctx;

    if (gpio >= gpio_count) {
        return;
    }

    channel = gpio_to_channel[gpio];
    if (channel < 0) {
        return;
    }

    now_us = time_us_64();
    ctx = &channels[channel];
    ctx->capture.last_edge_us = now_us;

    if ((events & GPIO_IRQ_EDGE_RISE) != 0u) {
        if (ctx->capture.have_rise && ctx->capture.have_high) {
            pwm_gpio_mon_publish_sample(ctx, now_us - ctx->capture.last_rise_us, ctx->capture.high_us, unstable_freq_hz, unstable_duty);
        }

        ctx->capture.last_rise_us = now_us;
        ctx->capture.have_rise = true;
        ctx->capture.have_high = false;
    }

    if ((events & GPIO_IRQ_EDGE_FALL) != 0u && ctx->capture.have_rise && now_us >= ctx->capture.last_rise_us) {
        uint64_t high_us = now_us - ctx->capture.last_rise_us;

        if (high_us > UINT32_MAX) {
            high_us = UINT32_MAX;
        }

        ctx->capture.high_us = (uint32_t)high_us;
        ctx->capture.have_high = true;
    }
}

/** @brief Read one GPIO monitor channel and apply static-level fallback when idle. */
static inline bool pwm_gpio_mon_read_channel(uint channel, pwm_driver_state_t *state, pwm_gpio_mon_channel_t *channels, uint32_t static_timeout_us, const uint *gpio_pins) {
    uint32_t irq_state;
    uint64_t last_edge_us;
    bool sample_valid;
    uint64_t now_us;

    irq_state = save_and_disable_interrupts();
    last_edge_us = channels[channel].capture.last_edge_us;
    sample_valid = channels[channel].sample_valid;
    now_us = time_us_64();
    if ((now_us - last_edge_us) >= static_timeout_us) {
        restore_interrupts(irq_state);
        state->freq_hz = 0u;
        state->duty = gpio_get(gpio_pins[channel]) ? 100u : 0u;
        state->pulse_count = 0u;
        return true;
    }

    *state = channels[channel].state;
    restore_interrupts(irq_state);
    return sample_valid;
}

#endif