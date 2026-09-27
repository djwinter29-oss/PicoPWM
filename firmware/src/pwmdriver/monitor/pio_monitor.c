/**
 * @file monitor.c
 * @brief Standalone Core 1 PIO PWM monitor backend prototype for the logical `pwmdriver` layer.
 *
 * This module measures the configured PIO monitor GPIO bank. Each backend-local channel owns one
 * PIO state machine that watches a single input pin and pushes one raw high-loop count followed
 * by one raw low-loop count for one requested PWM-period capture.
 *
 * The current prototype favors a small program and simple software decode over exact edge
 * timestamping. Exported `freq_hz` and `duty` therefore reflect approximate measurements
 * derived from the dominant two-instruction loop body rather than a fully compensated edge
 * timing model.
 *
 * Current limitations:
 * - Each read captures at most one complete PWM period. Intermediate periods and waveform
 *   history are intentionally discarded.
 * - The backend does not provide a reliable received pulse count and always reports
 *   `pulse_count = 0`.
 * - A capture waits up to one second for a complete period before reporting a static level.
 * - The exported frequency and duty cycle are approximate because the conversion ignores
 *   fixed setup and branch overhead around each measured high/low segment.
 * - Inputs slower than 1 Hz are intentionally treated as permanent high or low levels
 *   rather than as in-spec PWM measurements.
 */

#include "pio_monitor.h"

#include "pico/time.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "../pwm_driver_internal.h"
#include "pio_monitor_decode.h"
#include "pio_monitor.pio.h"

/** @brief Inactivity threshold used to treat a channel as a permanent level. */
#define PIO_MON_STATIC_TIMEOUT_US 1000000u

/** @brief Runtime ownership and last sample state for one PIO monitor channel. */
typedef struct {
    pwm_driver_state_t state;  /**< Latest exported monitor state. */
    uint64_t capture_start_us; /**< Timestamp when the current one-period capture started. */
    bool sample_valid;         /**< Indicates whether one full PWM period has been captured. */
    bool capture_active;       /**< Indicates that the PIO state machine is capturing one period. */

    PIO pio;    /**< Owning PIO block for the channel. */
    uint8_t sm; /**< Owning state machine index within @ref pio. */
} pio_mon_channel_t;

/** @brief Per-channel standalone PIO monitor runtime ownership table. */
static pio_mon_channel_t pio_mon_channels[PIO_PWM_DRIVER_COUNT];
/** @brief Cached program offsets per PIO block for the PIO monitor program; `UINT8_MAX` means unloaded. */
static uint8_t pio_mon_program_offsets[2] = {UINT8_MAX, UINT8_MAX};
/** @brief Guards the standalone monitor lifecycle so init only runs once. */
static bool pio_mon_initialized = false;
/** @brief Cached system clock used by the monitor decode path. */
static uint32_t pio_mon_sys_clk_hz = 0u;
/** @brief Map a Pico SDK PIO instance to a dense local array index. */
static uint8_t pio_mon_index(PIO pio) {
    return pio == pio0 ? 0u : 1u;
}

/** @brief Return the owning PIO block for one backend-local monitor channel index. */
static PIO pio_mon_pio_for_channel(uint channel) {
    return channel < 4u ? pio0 : pio1;
}

/** @brief Return the owning state machine index for one backend-local monitor channel index. */
static uint8_t pio_mon_sm_for_channel(uint channel) {
    return (uint8_t)(channel % 4u);
}

/** @brief Publish one exported monitor state and matching validity flag. */
static void pio_mon_publish_state(pio_mon_channel_t *ctx, uint32_t freq_hz, uint8_t duty, bool sample_valid) {
    ctx->state.freq_hz = freq_hz;
    ctx->state.duty = duty;
    ctx->state.pulse_count = 0u;
    ctx->sample_valid = sample_valid;
}

/** @brief Clear one channel's cached monitor sample and transient decode state. */
static void pio_mon_reset_channel(pio_mon_channel_t *ctx) {
    pio_mon_publish_state(ctx, 0u, 0u, false);
    ctx->capture_start_us = 0u;
    ctx->capture_active = false;
}

/** @brief Publish a permanent-high or permanent-low level after prolonged inactivity. */
static void pio_mon_publish_static_level(uint channel) {
    pio_mon_channel_t *ctx = &pio_mon_channels[channel];
    bool high = gpio_get(pwm_driver_get_gpio(PWM_DRIVER_CONFIG_BACKEND_PIO_MONITOR, channel));

    pio_mon_publish_state(ctx, 0u, high ? 100u : 0u, true);
}

/** @brief Publish the defined unstable-read sentinel state. */
static void pio_mon_publish_unstable(pio_mon_channel_t *ctx) {
    pio_mon_publish_state(ctx, PIO_MON_UNSTABLE_FREQ_HZ, PIO_MON_UNSTABLE_DUTY, false);
}

/** @brief Start one non-DMA PIO capture for a complete high/low period. */
static void pio_mon_start_capture(pio_mon_channel_t *ctx) {
    pio_sm_set_enabled(ctx->pio, ctx->sm, false);
    pio_sm_clear_fifos(ctx->pio, ctx->sm);
    pio_sm_restart(ctx->pio, ctx->sm);
    ctx->capture_start_us = time_us_64();
    ctx->capture_active = true;
    pio_sm_set_enabled(ctx->pio, ctx->sm, true);
}

/** @brief Read and decode the single high/low pair from the stopped PIO capture. */
static bool pio_mon_read_pair(pio_mon_channel_t *ctx, uint32_t *freq_hz, uint8_t *duty) {
    uint32_t high_ticks = pio_sm_get(ctx->pio, ctx->sm);
    uint32_t low_ticks = pio_sm_get(ctx->pio, ctx->sm);

    pio_sm_set_enabled(ctx->pio, ctx->sm, false);
    ctx->capture_active = false;
    return pio_monitor_decode_pair(high_ticks, low_ticks, pio_mon_sys_clk_hz, freq_hz, duty);
}

/** @brief Poll one one-period capture and refresh the backend-local monitor state. */
static void pio_mon_refresh_channel(uint channel) {
    pio_mon_channel_t *ctx = &pio_mon_channels[channel];
    uint64_t now_us = time_us_64();

    if (!ctx->capture_active) {
        pio_mon_start_capture(ctx);
        return;
    }

    if (pio_sm_get_rx_fifo_level(ctx->pio, ctx->sm) >= 2u) {
        uint32_t freq_hz;
        uint8_t duty;

        if (pio_mon_read_pair(ctx, &freq_hz, &duty)) {
            pio_mon_publish_state(ctx, freq_hz, duty, true);
        } else {
            pio_mon_publish_unstable(ctx);
        }
        return;
    }

    if ((now_us - ctx->capture_start_us) >= PIO_MON_STATIC_TIMEOUT_US) {
        pio_sm_set_enabled(ctx->pio, ctx->sm, false);
        ctx->capture_active = false;
        pio_mon_publish_static_level(channel);
    }
}

/** @copydoc pio_mon_init */
bool pio_mon_init(void) {
    if (pio_mon_initialized) {
        return true;
    }

    pio_mon_sys_clk_hz = clock_get_hz(clk_sys);

    if (pio_mon_program_offsets[0] == UINT8_MAX) {
        pio_mon_program_offsets[0] = pio_add_program(pio0, &monitor_program);
    }

    if (pio_mon_program_offsets[1] == UINT8_MAX) {
        pio_mon_program_offsets[1] = pio_add_program(pio1, &monitor_program);
    }

    for (uint channel = 0; channel < pwm_driver_config_backend_channel_count(PWM_DRIVER_CONFIG_BACKEND_PIO_MONITOR);
         channel++) {
        pio_mon_channel_t *ctx = &pio_mon_channels[channel];
        uint pin = pwm_driver_get_gpio(PWM_DRIVER_CONFIG_BACKEND_PIO_MONITOR, channel);
        uint8_t program_offset;

        ctx->pio = pio_mon_pio_for_channel(channel);
        ctx->sm = pio_mon_sm_for_channel(channel);
        pio_mon_reset_channel(ctx);
        program_offset = pio_mon_program_offsets[pio_mon_index(ctx->pio)];

        monitor_program_init(ctx->pio, ctx->sm, program_offset, pin);
        pio_sm_set_enabled(ctx->pio, ctx->sm, false);
        pio_sm_clear_fifos(ctx->pio, ctx->sm);
        pio_sm_restart(ctx->pio, ctx->sm);
    }

    pio_mon_initialized = true;
    return true;
}

/** @copydoc pio_mon_get */
bool pio_mon_get(uint channel, pwm_driver_state_t *state) {
    if (!pio_mon_initialized ||
        channel >= pwm_driver_config_backend_channel_count(PWM_DRIVER_CONFIG_BACKEND_PIO_MONITOR) || state == NULL) {
        return false;
    }

    pio_mon_refresh_channel(channel);
    *state = pio_mon_channels[channel].state;
    return pio_mon_channels[channel].sample_valid;
}