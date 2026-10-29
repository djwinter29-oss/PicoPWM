/**
 * @file software_monitor.c
 * @brief Core 1 timer-sampled, low-frequency GPIO monitor with bounded work per scan.
 */

#include "software_monitor.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/time.h"

/** @brief Minimum delay between completion of one scan and the next scan. */
#define SW_MON_SCAN_US 10
/** @brief Accumulate complete cycles over at least this measurement window. */
#define SW_MON_WINDOW_US 20000u
/** @brief Treat a channel with no observed transitions for this long as static. */
#define SW_MON_STATIC_TIMEOUT_US 1000000u

/** @brief Timer-owned capture state; Core 1 readers copy it with interrupts masked. */
typedef struct {
    uint64_t last_edge_us; /**< Timestamp of the latest sampled transition. */
    uint64_t last_rise_us; /**< Timestamp of the latest sampled rising edge. */
    uint32_t high_us;      /**< High width of the current complete cycle. */
    uint32_t period_sum;   /**< Elapsed time accumulated across complete cycles. */
    uint32_t high_sum;     /**< High time accumulated across those cycles. */
    uint32_t cycles;       /**< Number of accumulated complete cycles. */
    uint32_t sample_us;    /**< Elapsed time of the latest published window; zero before a sample. */
    uint32_t sample_high;  /**< High time of the latest published window. */
    uint32_t sample_count; /**< Complete cycles in the latest published window. */
    bool have_rise;        /**< A starting rising edge has been observed. */
    bool have_high;        /**< A falling edge has completed the current high segment. */
} sw_mon_channel_t;

/** @brief Capture state for the eight fixed software-bank GPIOs. */
static sw_mon_channel_t sw_mon_channels[PWM_DRIVER_CONFIG_BANK_SIZE];
/** @brief Private pool created on the backend-owning core. */
static alarm_pool_t *sw_mon_pool;
/** @brief One timer for the entire monitor bank, independent of input edge rate. */
static repeating_timer_t sw_mon_timer;
/** @brief GPIO levels from the preceding bitmap read. */
static uint32_t sw_mon_levels;
/** @brief True only after timer registration succeeds. */
static bool sw_mon_initialized;

/** @brief Sample all inputs once and accumulate complete observed cycles without division. */
static bool sw_mon_scan(repeating_timer_t *timer) {
    (void)timer;
    uint32_t levels = gpio_get_all();
    uint32_t changed = levels ^ sw_mon_levels;
    uint64_t now = time_us_64();

    sw_mon_levels = levels;
    for (uint channel = 0; channel < PWM_DRIVER_CONFIG_BANK_SIZE; ++channel) {
        uint32_t mask = 1u << software_pwm_config.channels[channel].gpio;
        if ((changed & mask) == 0u) {
            continue;
        }
        sw_mon_channel_t *ctx = &sw_mon_channels[channel];
        if (now - ctx->last_edge_us >= SW_MON_STATIC_TIMEOUT_US) {
            *ctx = (sw_mon_channel_t){0};
        }
        ctx->last_edge_us = now;
        if ((levels & mask) != 0u) {
            if (ctx->have_rise && ctx->have_high) {
                uint64_t period = now - ctx->last_rise_us;
                if (period <= SW_MON_STATIC_TIMEOUT_US && ctx->high_us <= period) {
                    ctx->period_sum += (uint32_t)period;
                    ctx->high_sum += ctx->high_us;
                    ctx->cycles++;
                    if (ctx->period_sum >= SW_MON_WINDOW_US) {
                        ctx->sample_us = ctx->period_sum;
                        ctx->sample_high = ctx->high_sum;
                        ctx->sample_count = ctx->cycles;
                        ctx->period_sum = ctx->high_sum = ctx->cycles = 0u;
                    }
                } else {
                    *ctx = (sw_mon_channel_t){.last_edge_us = now};
                }
            }
            ctx->last_rise_us = now;
            ctx->have_rise = true;
            ctx->have_high = false;
        } else if (ctx->have_rise) {
            uint64_t high = now - ctx->last_rise_us;
            ctx->high_us = high > UINT32_MAX ? UINT32_MAX : (uint32_t)high;
            ctx->have_high = true;
        }
    }
    return true;
}

/** @copydoc sw_mon_init */
bool sw_mon_init(void) {
    if (sw_mon_initialized) {
        return true;
    }
    if (sw_mon_pool == NULL) {
        sw_mon_pool = alarm_pool_create_with_unused_hardware_alarm(1);
        if (sw_mon_pool == NULL) {
            return false;
        }
    }
    if (alarm_pool_core_num(sw_mon_pool) != get_core_num()) {
        return false;
    }
    uint64_t now = time_us_64();
    for (uint channel = 0; channel < PWM_DRIVER_CONFIG_BANK_SIZE; ++channel) {
        uint pin = software_pwm_config.channels[channel].gpio;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        sw_mon_channels[channel] = (sw_mon_channel_t){.last_edge_us = now};
    }
    sw_mon_levels = gpio_get_all();
    /* ponytail: completion-relative scans avoid catch-up IRQ storms under load; actual timestamps
     * account for variable spacing, but narrow pulses and out-of-range inputs can alias. Use PIO
     * if sampled low-frequency monitoring is insufficient. */
    if (!alarm_pool_add_repeating_timer_us(sw_mon_pool, SW_MON_SCAN_US, sw_mon_scan, NULL, &sw_mon_timer)) {
        return false;
    }
    sw_mon_initialized = true;
    return true;
}

/** @copydoc sw_mon_get */
bool sw_mon_get(uint channel, pwm_state_t *state) {
    if (!sw_mon_initialized || channel >= PWM_DRIVER_CONFIG_BANK_SIZE || state == NULL) {
        return false;
    }
    uint32_t irq_state = save_and_disable_interrupts();
    sw_mon_channel_t ctx = sw_mon_channels[channel];
    uint64_t now = time_us_64();
    uint32_t levels = sw_mon_levels;
    restore_interrupts(irq_state);

    if (now - ctx.last_edge_us >= SW_MON_STATIC_TIMEOUT_US) {
        state->freq_hz = 0u;
        state->duty = (levels & (1u << software_pwm_config.channels[channel].gpio)) ? 100u : 0u;
        return true;
    }
    if (ctx.sample_us == 0u) {
        state->freq_hz = SW_MON_UNSTABLE_FREQ_HZ;
        state->duty = SW_MON_UNSTABLE_DUTY;
        return false;
    }
    state->freq_hz = (uint32_t)(((uint64_t)ctx.sample_count * 1000000u + ctx.sample_us / 2u) / ctx.sample_us);
    state->duty = (uint8_t)(((uint64_t)ctx.sample_high * 100u + ctx.sample_us / 2u) / ctx.sample_us);
    return true;
}
