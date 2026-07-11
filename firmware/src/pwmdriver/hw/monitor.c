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
/* ponytail: This lookup table is sized to the current Pico GPIO count assumption.
 * The ceiling is platform drift if a future target exposes a different GPIO range. That is
 * acceptable now because the current RP2040/RP2350 Pico pin bank used by this firmware fits
 * within this bound. If board support widens later, replace this with an SDK- or board-level
 * GPIO-limit constant.
 */
/** @brief GPIO count used for the direct GPIO-to-channel lookup table. */
#define HW_MON_GPIO_COUNT 30u

/** @brief Transient edge-tracking state for one hardware monitor channel. */
typedef struct {
    uint64_t last_edge_us; /**< Timestamp of the most recent observed GPIO transition. */
    uint64_t last_rise_us; /**< Timestamp of the most recent observed rising edge. */
    uint32_t high_us; /**< Cached high duration captured after the most recent rising edge. */
    bool have_rise; /**< Indicates whether the monitor has observed at least one rising edge. */
    bool have_high; /**< Indicates whether the monitor has captured a high width for the active cycle. */
} hw_mon_capture_t;

/** @brief Runtime ownership and latest sample state for one hardware monitor channel. */
typedef struct {
    pwm_driver_state_t state; /**< Latest exported monitor state. */
    bool sample_valid; /**< Indicates whether one full PWM sample has been captured. */
    hw_mon_capture_t capture; /**< Transient edge timestamps and partial-sample tracking. */
} hw_mon_channel_t;

/** @brief Per-channel standalone hardware monitor runtime ownership table. */
static hw_mon_channel_t hw_mon_channels[HW_PWM_COUNT];
/** @brief Direct GPIO-to-channel lookup table; `-1` marks unrelated GPIOs. */
static int8_t hw_mon_gpio_to_channel[HW_MON_GPIO_COUNT] = {
    [0 ... HW_MON_GPIO_COUNT - 1] = -1
};
/** @brief Guards the standalone hardware monitor lifecycle so init only runs once. */
static bool hw_mon_initialized = false;

/** @brief Publish one exported monitor state and matching validity flag. */
static void hw_mon_publish_state(hw_mon_channel_t *ctx, uint32_t freq_hz, uint8_t duty, bool sample_valid) {
    ctx->state.freq_hz = freq_hz;
    ctx->state.duty = duty;
    ctx->state.pulse_count = 0u;
    ctx->sample_valid = sample_valid;
}

/** @brief Configure one monitored GPIO as an input before IRQ arming. */
static void hw_mon_init_pin(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
}

/** @brief Clear one channel's cached monitor sample and transient decode state. */
static void hw_mon_reset_channel(hw_mon_channel_t *ctx) {
    uint64_t now_us = time_us_64();

    hw_mon_publish_state(ctx, 0u, 0u, false);
    ctx->capture.last_edge_us = now_us;
    ctx->capture.last_rise_us = now_us;
    ctx->capture.high_us = 0u;
    ctx->capture.have_rise = false;
    ctx->capture.have_high = false;
}

/** @brief Publish the defined unstable-read sentinel state. */
static void hw_mon_publish_unstable(hw_mon_channel_t *ctx) {
    /* ponytail: The unstable sentinel is retained to match the PIO monitor contract.
     * The ceiling is that this simpler hardware monitor could arguably use validity alone.
     * That tradeoff is acceptable now because cross-backend monitor consistency is more useful
     * than shaving one publication state. If the monitor APIs diverge later, revisit this.
     */
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

/** @brief Shared GPIO edge callback that timestamps hardware-monitor pin transitions. */
static void hw_mon_gpio_irq(uint gpio, uint32_t events) {
    int channel;
    uint64_t now_us;
    hw_mon_channel_t *ctx;

    if (gpio >= HW_MON_GPIO_COUNT) {
        return;
    }

    channel = hw_mon_gpio_to_channel[gpio];
    if (channel < 0) {
        return;
    }

    now_us = time_us_64();
    ctx = &hw_mon_channels[channel];
    ctx->capture.last_edge_us = now_us;

    if ((events & GPIO_IRQ_EDGE_RISE) != 0u) {
        /* One exported sample is defined as one high width plus the following full period. */
        if (ctx->capture.have_rise && ctx->capture.have_high) {
            hw_mon_publish_sample(ctx, now_us - ctx->capture.last_rise_us, ctx->capture.high_us);
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

/** @brief Synthesize one idle static-level state after prolonged lack of observed edges. */
static void hw_mon_read_idle_state(uint channel, pwm_driver_state_t *state) {
    state->freq_hz = 0u;
    state->duty = gpio_get(PWM_HW_GPIO_PINS[channel]) ? 100u : 0u;
    state->pulse_count = 0u;
}

/** @brief Read one channel state under interrupt exclusion and apply static-level fallback when idle. */
static bool hw_mon_read_channel(uint channel, pwm_driver_state_t *state) {
    uint32_t irq_state;
    uint64_t last_edge_us;
    bool sample_valid;
    uint64_t now_us;

    irq_state = save_and_disable_interrupts();
    last_edge_us = hw_mon_channels[channel].capture.last_edge_us;
    sample_valid = hw_mon_channels[channel].sample_valid;
    now_us = time_us_64();
    if ((now_us - last_edge_us) >= HW_MON_STATIC_TIMEOUT_US) {
        restore_interrupts(irq_state);
        hw_mon_read_idle_state(channel, state);
        return true;
    }

    *state = hw_mon_channels[channel].state;
    restore_interrupts(irq_state);
    return sample_valid;
}

/** @copydoc hw_mon_init */
void hw_mon_init(void) {
    if (hw_mon_initialized) {
        return;
    }

    for (uint channel = 0; channel < HW_PWM_COUNT; channel++) {
        uint pin = PWM_HW_GPIO_PINS[channel];

        hw_mon_init_pin(pin);

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