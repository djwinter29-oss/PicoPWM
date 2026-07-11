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
/** @brief Idle-state period tick value used when one channel is not scheduled. */
#define SW_GEN_IDLE_PERIOD_TICKS 1000u
/** @brief Idle-state half-duty tick value used for the logical power-on default state. */
#define SW_GEN_IDLE_HALF_DUTY_TICKS 500u

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

/** @brief Publish one channel's realized software PWM state into the shared driver snapshot. */
static void sw_gen_publish_state(uint channel) {
    pwm_driver_state_t state;
    sw_pwm_channel_t *ch = &sw_pwm_channels[channel];

    state.freq_hz = ch->realized_freq_hz;
    state.duty = ch->realized_duty;
    state.pulse_count = ch->pulse_count;
    pwm_driver_store_applied_state(SW_PWM_CHANNEL_BASE + channel, &state);
}

/** @brief Drive one software PWM channel to a static level and publish matching realized state. */
static void sw_gen_drive_static_level(uint channel, bool high) {
    sw_pwm_channel_t *ch = &sw_pwm_channels[channel];
    uint32_t save = save_and_disable_interrupts();

    sw_pwm_active_mask &= ~(1u << channel);
    ch->period_ticks = SW_GEN_IDLE_PERIOD_TICKS;
    ch->duty_ticks = high ? SW_GEN_IDLE_PERIOD_TICKS : 0u;
    ch->counter = 0;
    ch->realized_freq_hz = 0u;
    ch->realized_duty = high ? 100u : 0u;
    restore_interrupts(save);

    gpio_put(ch->gpio, high);
    sw_gen_publish_state(channel);
}

/** @brief Arm one software PWM channel for scheduled output and publish its active runtime state atomically. */
static void sw_gen_start_scheduled_output(uint channel, uint32_t period_ticks, uint32_t duty_ticks) {
    sw_pwm_channel_t *ch = &sw_pwm_channels[channel];
    uint32_t save = save_and_disable_interrupts();

    ch->period_ticks = period_ticks;
    ch->duty_ticks = duty_ticks;
    ch->counter = 0;
    sw_pwm_active_mask |= (1u << channel);
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
        sw_pwm_channels[i].period_ticks = SW_GEN_IDLE_PERIOD_TICKS;
        sw_pwm_channels[i].duty_ticks = SW_GEN_IDLE_HALF_DUTY_TICKS;
        sw_pwm_channels[i].counter = 0;
        sw_pwm_channels[i].realized_freq_hz = 0u;
        sw_pwm_channels[i].realized_duty = 50u;
        sw_pwm_channels[i].pulse_count = 0;

        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, 0);
    }

    add_repeating_timer_us(-SW_PWM_TICK_US, sw_pwm_tick_callback, NULL, &sw_pwm_timer);
}

/** @copydoc sw_gen_set_freq */
bool sw_gen_set_freq(uint channel, uint32_t freq_hz, uint8_t duty) {
    uint32_t period;
    uint32_t duty_ticks;
    sw_pwm_channel_t *ch;
    bool static_high;

    if (channel >= SW_PWM_COUNT) return false;
    if (duty > 100u) duty = 100u;

    ch = &sw_pwm_channels[channel];
    ch->realized_duty = duty;

    if (freq_hz == 0u) {
        sw_gen_drive_static_level(channel, duty >= 100u);
        return true;
    }

    if (duty == 0u || duty >= 100u) {
        static_high = duty >= 100u;
        sw_gen_drive_static_level(channel, static_high);
        return true;
    }

    period = (SW_PWM_BASE_HZ + (freq_hz / 2u)) / freq_hz;
    if (period < 1) period = 1;

    duty_ticks = (uint32_t)(((uint64_t)period * duty + 50u) / 100u);
    if (duty_ticks > period) duty_ticks = period;

    sw_gen_start_scheduled_output(channel, period, duty_ticks);

    ch->realized_freq_hz = (SW_PWM_BASE_HZ + (period / 2u)) / period;
    sw_gen_publish_state(channel);

    return true;
}

/** @copydoc sw_gen_get */
bool sw_gen_get(uint channel, pwm_driver_state_t *state) {
    sw_pwm_channel_t *ch;

    if (channel >= SW_PWM_COUNT || state == NULL) return false;
    ch = &sw_pwm_channels[channel];
    state->freq_hz = ch->realized_freq_hz;
    state->duty = ch->realized_duty;
    state->pulse_count = ch->pulse_count;
    return true;
}