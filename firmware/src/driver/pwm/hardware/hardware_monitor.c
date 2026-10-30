// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

/**
 * @file hardware_monitor.c
 * @brief Hardware PWM-slice input monitor for the logical `pwm` layer.
 *
 * Each bank-A channel uses its slice's counter with the slice-B pin as the input. A slice has
 * one counter, so every channel time-shares two measurement windows on Core 1:
 * 1. edge window: `PWM_DIV_B_RISING` counts rising edges, giving frequency;
 * 2. high window: `PWM_DIV_B_HIGH` counts divided sys-clock ticks while the input is high,
 *    giving duty against the measured window time.
 * Counter wraps are accumulated by polling the slice wrap flag on every `hw_mon_get()` call.
 */

#include "hardware_monitor.h"

#include "pico/time.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "../pwm_driver_internal.h"

/**
 * ponytail: fixed window length. The ceiling is the low end: frequency resolution is
 * 1 / window (4 Hz) and a sample needs several periods per window, so the monitor is only
 * reliable above about 20 Hz. That is acceptable for the hardware bank's high-rate role; use
 * the software monitor for slow signals, or make the window adaptive if this ever matters.
 */
#define HW_MON_WINDOW_US 250000u
/** @brief Slice clock divider in high-time mode; keeps one 1 ms poll shorter than one counter wrap. */
#define HW_MON_HIGH_DIV 4u
/** @brief Counter span between wraps of the free-running 16-bit slice counter. */
#define HW_MON_COUNTER_SPAN 65536u

/** @brief Measurement window currently running on one slice. */
typedef enum {
    HW_MON_PHASE_EDGES = 0, /**< Counting rising edges. */
    HW_MON_PHASE_HIGH,      /**< Counting sys-clock ticks while the input is high. */
} hw_mon_phase_t;

/** @brief Runtime state for one hardware monitor channel. */
typedef struct {
    uint slice;              /**< PWM slice measuring this channel. */
    uint gpio;               /**< Slice-B input GPIO. */
    hw_mon_phase_t phase;    /**< Window currently running. */
    uint64_t phase_start_us; /**< Start time of the current window. */
    uint32_t wraps;          /**< Counter wraps accumulated in the current window. */
    uint32_t edge_freq_hz;   /**< Frequency from the last completed edge window. */
    pwm_state_t sample;      /**< Last completed measurement. */
    bool sample_valid;       /**< True after the first completed edge and high window pair. */
} hw_mon_channel_t;

static hw_mon_channel_t hw_mon_channels[PWM_DRIVER_CONFIG_BANK_SIZE];
static uint32_t hw_mon_sys_clk_hz;
static bool hw_mon_initialized = false;

/** @brief Start one window on a channel: select the slice input mode and zero its counter. */
static void hw_mon_start_phase(hw_mon_channel_t *ch, hw_mon_phase_t phase) {
    pwm_config cfg = pwm_get_default_config();

    pwm_config_set_clkdiv_mode(&cfg, phase == HW_MON_PHASE_EDGES ? PWM_DIV_B_RISING : PWM_DIV_B_HIGH);
    pwm_config_set_clkdiv(&cfg, phase == HW_MON_PHASE_EDGES ? 1.0f : (float)HW_MON_HIGH_DIV);
    pwm_init(ch->slice, &cfg, true);
    pwm_set_counter(ch->slice, 0u);
    pwm_clear_irq(ch->slice);
    ch->phase = phase;
    ch->wraps = 0u;
    ch->phase_start_us = time_us_64();
}

/** @brief Return the window count so far, folding in counter wraps without double counting. */
static uint64_t hw_mon_read_count(hw_mon_channel_t *ch) {
    const uint32_t mask = 1u << ch->slice;

    for (;;) {
        if ((pwm_hw->intr & mask) != 0u) {
            pwm_clear_irq(ch->slice);
            ch->wraps++;
        }
        uint32_t counter = pwm_get_counter(ch->slice);
        if ((pwm_hw->intr & mask) == 0u) {
            return (uint64_t)ch->wraps * HW_MON_COUNTER_SPAN + counter;
        }
    }
}

/** @brief Advance one channel's window state machine and publish a sample when a pair completes. */
static void hw_mon_poll(hw_mon_channel_t *ch) {
    uint64_t count = hw_mon_read_count(ch);
    uint64_t elapsed_us = time_us_64() - ch->phase_start_us;

    if (elapsed_us < HW_MON_WINDOW_US) {
        return;
    }

    if (ch->phase == HW_MON_PHASE_EDGES) {
        ch->edge_freq_hz = (uint32_t)((count * 1000000u + elapsed_us / 2u) / elapsed_us);
        hw_mon_start_phase(ch, HW_MON_PHASE_HIGH);
        return;
    }

    uint64_t duty = (count * HW_MON_HIGH_DIV * 100u * 1000000u + (uint64_t)hw_mon_sys_clk_hz * elapsed_us / 2u) /
                    ((uint64_t)hw_mon_sys_clk_hz * elapsed_us);

    ch->sample.freq_hz = ch->edge_freq_hz;
    ch->sample.duty =
        (ch->edge_freq_hz == 0u) ? (gpio_get(ch->gpio) ? 100u : 0u) : (duty > 100u ? 100u : (uint8_t)duty);
    ch->sample_valid = true;
    hw_mon_start_phase(ch, HW_MON_PHASE_EDGES);
}

/** @copydoc hw_mon_init */
bool hw_mon_init(void) {
    if (hw_mon_initialized) {
        return true;
    }

    hw_mon_sys_clk_hz = clock_get_hz(clk_sys);
    for (uint i = 0u; i < PWM_DRIVER_CONFIG_BANK_SIZE; i++) {
        hw_mon_channel_t *ch = &hw_mon_channels[i];

        ch->gpio = hardware_pwm_config.channels[i].gpio;
        ch->slice = pwm_gpio_to_slice_num(ch->gpio);
        gpio_set_function(ch->gpio, GPIO_FUNC_PWM);
        hw_mon_start_phase(ch, HW_MON_PHASE_EDGES);
    }

    hw_mon_initialized = true;
    return true;
}

/** @copydoc hw_mon_get */
bool hw_mon_get(uint channel, pwm_state_t *state) {
    if (!hw_mon_initialized || channel >= PWM_DRIVER_CONFIG_BANK_SIZE || state == NULL) {
        return false;
    }

    hw_mon_poll(&hw_mon_channels[channel]);
    if (!hw_mon_channels[channel].sample_valid) {
        return false;
    }
    *state = hw_mon_channels[channel].sample;
    return true;
}
