/**
 * @file generator.c
 * @brief Software PWM generator backend implementation for the logical `pwmdriver` layer.
 */

#include "generator.h"

#include "../pwm_driver.h"
#include "../pwm_driver_internal.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"

/** @brief Software PWM scheduler tick period in microseconds. */
#define SW_PWM_TICK_US 10
/** @brief Base tick frequency derived from @ref SW_PWM_TICK_US. */
#define SW_PWM_BASE_HZ 100000u

/** @brief Runtime scheduling state for one software PWM backend channel. */
typedef struct {
    uint gpio; /**< Assigned output GPIO. */
    uint32_t period_ticks; /**< Whole-tick PWM period for the current configuration. */
    uint32_t duty_ticks; /**< Whole-tick active window for the current configuration. */
    uint32_t counter; /**< Current tick position within the PWM period. */
    uint32_t realized_freq_hz; /**< Realized output frequency in Hz. */
    uint8_t realized_duty; /**< Realized duty in percent in the range `[0, 100]`. */
    volatile uint32_t pulse_count; /**< Monotonic generated-period count from power-on. */
} sw_pwm_channel_t;

/** @brief Software PWM runtime state in backend-local channel order. */
static sw_pwm_channel_t sw_pwm_channels[SW_PWM_COUNT];
/** @brief Repeating timer that drives the software PWM scheduler. */
static repeating_timer_t sw_pwm_timer;
/** @brief Bitmask of backend-local software PWM channels currently driven by the scheduler. */
static uint32_t sw_pwm_active_mask = 0u;

/** @brief Fully resolved software-PWM static-output target consumed by the apply path. */
typedef struct {
    uint8_t realized_duty; /**< Realized output duty in percent. */
    bool high; /**< Output level to drive while the channel is static. */
} sw_gen_static_target_t;

/** @brief Build one caller-owned software PWM snapshot from the backend-local channel state. */
static pwm_driver_state_t sw_gen_channel_state(const sw_pwm_channel_t *ch) {
    return (pwm_driver_state_t) {
        .freq_hz = ch->realized_freq_hz,
        .duty = ch->realized_duty,
        .pulse_count = ch->pulse_count,
    };
}

/** @brief Publish one channel's realized software PWM state into the shared driver snapshot. */
static void sw_gen_publish_state(uint channel) {
    sw_pwm_channel_t *ch = &sw_pwm_channels[channel];
    pwm_driver_state_t state = sw_gen_channel_state(ch);

    pwm_driver_store_applied_state(SW_PWM_CHANNEL_BASE + channel, &state);
}

/** @brief Publish one channel's realized software PWM state while the caller already owns interrupt exclusion. */
static void sw_gen_publish_state_coherent(uint channel) {
    sw_pwm_channel_t *ch = &sw_pwm_channels[channel];
    pwm_driver_state_t state = sw_gen_channel_state(ch);

    pwm_driver_store_applied_state_coherent(SW_PWM_CHANNEL_BASE + channel, &state, time_us_64());
}

/** @brief Resolve whether one software-PWM request should be handled as a static-output mode. */
static bool sw_gen_resolve_static_target(uint32_t freq_hz, uint8_t duty, sw_gen_static_target_t *target) {
    if (target == NULL) {
        return false;
    }

    if (freq_hz != 0u && duty != 0u && duty < 100u) {
        return false;
    }

    target->high = duty >= 100u;
    target->realized_duty = target->high ? 100u : 0u;
    return true;
}

/** @brief Drive one software PWM channel to a static level and publish matching realized state. */
static void sw_gen_drive_static_level(uint channel, uint8_t realized_duty, bool high) {
    sw_pwm_channel_t *ch = &sw_pwm_channels[channel];
    uint32_t save = save_and_disable_interrupts();

    sw_pwm_active_mask &= ~(1u << channel);
    ch->period_ticks = 0u;
    ch->duty_ticks = 0u;
    ch->counter = 0u;
    ch->realized_freq_hz = 0u;
    ch->realized_duty = realized_duty;
    restore_interrupts(save);

    gpio_put(ch->gpio, high);
    sw_gen_publish_state(channel);
}

/** @brief Arm one software PWM channel for scheduled output and publish its active runtime state atomically. */
static void sw_gen_start_scheduled_output(uint channel, uint32_t period_ticks, uint32_t duty_ticks, uint32_t realized_freq_hz, uint8_t realized_duty) {
    sw_pwm_channel_t *ch = &sw_pwm_channels[channel];
    uint32_t save = save_and_disable_interrupts();

    ch->period_ticks = period_ticks;
    ch->duty_ticks = duty_ticks;
    ch->counter = 0u;
    ch->realized_freq_hz = realized_freq_hz;
    ch->realized_duty = realized_duty;
    sw_pwm_active_mask |= (1u << channel);
    sw_gen_publish_state_coherent(channel);
    restore_interrupts(save);
}

/** @brief Repeating timer callback that advances all active software PWM channels. */
static bool sw_pwm_tick_callback(repeating_timer_t *rt) {
    uint32_t active_mask;

    (void)rt;

    active_mask = sw_pwm_active_mask;
    while (active_mask != 0u) {
        uint32_t channel = (uint32_t)__builtin_ctz(active_mask);
        sw_pwm_channel_t *ch = &sw_pwm_channels[channel];

        active_mask &= active_mask - 1u;

        ch->counter++;
        if (ch->counter >= ch->period_ticks) {
            ch->counter = 0;
            ch->pulse_count++;
            pwm_driver_store_pulse_count(SW_PWM_CHANNEL_BASE + channel, ch->pulse_count);
        }
        gpio_put(ch->gpio, ch->counter < ch->duty_ticks);
    }

    return true;
}

/** @copydoc sw_gen_init */
void sw_gen_init(void) {
    sw_pwm_active_mask = 0u;

    for (int i = 0; i < SW_PWM_COUNT; i++) {
        uint gpio = PWM_SW_GPIO_PINS[i];

        sw_pwm_channels[i].gpio = gpio;
        sw_pwm_channels[i].period_ticks = 0u;
        sw_pwm_channels[i].duty_ticks = 0u;
        sw_pwm_channels[i].counter = 0u;
        sw_pwm_channels[i].realized_freq_hz = 0u;
        sw_pwm_channels[i].realized_duty = 50u;
        sw_pwm_channels[i].pulse_count = 0;

        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, 0);
    }

    add_repeating_timer_us(-SW_PWM_TICK_US, sw_pwm_tick_callback, NULL, &sw_pwm_timer);
}

/** @copydoc sw_gen_set */
bool sw_gen_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    sw_gen_static_target_t static_target;
    uint32_t period;
    uint32_t duty_ticks;
    uint32_t realized_freq_hz;

    if (channel >= SW_PWM_COUNT) return false;
    if (duty > 100u) duty = 100u;

    if (sw_gen_resolve_static_target(freq_hz, duty, &static_target)) {
        sw_gen_drive_static_level(channel, static_target.realized_duty, static_target.high);
        return true;
    }

    if (freq_hz > SW_GEN_MAX_FREQ_HZ) {
        return false;
    }

    period = (SW_PWM_BASE_HZ + (freq_hz / 2u)) / freq_hz;
    if (period < 1) period = 1;

    duty_ticks = (uint32_t)(((uint64_t)period * duty + 50u) / 100u);
    if (duty_ticks > period) duty_ticks = period;

    realized_freq_hz = (SW_PWM_BASE_HZ + (period / 2u)) / period;
    sw_gen_start_scheduled_output(channel, period, duty_ticks, realized_freq_hz, duty);

    return true;
}

/** @copydoc sw_gen_restore_defaults */
bool sw_gen_restore_defaults(void) {
    uint32_t save = save_and_disable_interrupts();

    /* Phase 1: clear coherent runtime state while the scheduler cannot observe partial reset. */
    sw_pwm_active_mask = 0u;
    for (uint channel = 0; channel < SW_PWM_COUNT; channel++) {
        sw_pwm_channel_t *ch = &sw_pwm_channels[channel];

        ch->period_ticks = 0u;
        ch->duty_ticks = 0u;
        ch->counter = 0u;
        ch->realized_freq_hz = 0u;
        ch->realized_duty = 50u;
    }
    restore_interrupts(save);

    /* Phase 2: publish the reset output level and shared snapshot after the coherent state reset. */
    for (uint channel = 0; channel < SW_PWM_COUNT; channel++) {
        gpio_put(sw_pwm_channels[channel].gpio, 0);
        sw_gen_publish_state(channel);
    }

    return true;
}
