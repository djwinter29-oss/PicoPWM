/**
 * @file sw_pwm_driver.c
 * @brief Software PWM backend implementation for the logical `pwmdriver` layer.
 */

#include "sw_pwm_driver.h"

#include "pwm_driver.h"
#include "pwm_driver_internal.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"

/** @brief Software PWM scheduler tick period in microseconds. */
#define SW_PWM_TICK_US 10
/** @brief Base tick frequency derived from @ref SW_PWM_TICK_US. */
#define SW_PWM_BASE_HZ 100000.0f

/** @brief Runtime scheduling state for one software PWM backend channel. */
typedef struct {
    uint gpio; /**< Assigned output GPIO. */
    uint32_t period_ticks; /**< Whole-tick PWM period for the current configuration. */
    uint32_t duty_ticks; /**< Whole-tick active window for the current configuration. */
    uint32_t counter; /**< Current tick position within the PWM period. */
    volatile uint32_t pulse_count; /**< Monotonic generated-period count from power-on. */
    bool active; /**< Indicates whether the channel currently drives output. */
} sw_pwm_channel_t;

/** @brief Software PWM runtime state in backend-local channel order. */
static sw_pwm_channel_t sw_pwm_channels[SW_PWM_COUNT];
/** @brief Repeating timer that drives the software PWM scheduler. */
static repeating_timer_t sw_pwm_timer;

/** @brief Realized software PWM frequencies in backend-local channel order. */
static float actual_freqs[SW_PWM_COUNT] = {0};
/** @brief Realized software PWM duties in backend-local channel order. */
static float duties[SW_PWM_COUNT] = {0};
/** @brief Enabled-state cache for software PWM backend channels. */
static bool enabled[SW_PWM_COUNT] = {false};

/** @brief Repeating timer callback that advances all active software PWM channels. */
static bool sw_pwm_tick_callback(repeating_timer_t *rt) {
    (void)rt;

    for (int i = 0; i < SW_PWM_COUNT; i++) {
        sw_pwm_channel_t *ch = &sw_pwm_channels[i];
        if (!ch->active) continue;

        ch->counter++;
        if (ch->counter >= ch->period_ticks) {
            ch->counter = 0;
            ch->pulse_count++;
            pwm_driver_store_pulse_count(SW_PWM_CHANNEL_BASE + i, ch->pulse_count);
        }
        gpio_put(ch->gpio, ch->counter < ch->duty_ticks);
    }

    return true;
}

/** @copydoc sw_pwm_driver_init */
void sw_pwm_driver_init(void) {
    for (int i = 0; i < SW_PWM_COUNT; i++) {
        uint gpio = PWM_SW_GPIO_PINS[i];

        sw_pwm_channels[i].gpio = gpio;
        sw_pwm_channels[i].period_ticks = 1000;
        sw_pwm_channels[i].duty_ticks = 500;
        sw_pwm_channels[i].counter = 0;
        sw_pwm_channels[i].pulse_count = 0;
        sw_pwm_channels[i].active = false;

        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, 0);

        actual_freqs[i] = 0.0f;
        duties[i] = 0.5f;
        enabled[i] = false;
    }

    add_repeating_timer_us(-SW_PWM_TICK_US, sw_pwm_tick_callback, NULL, &sw_pwm_timer);
}

/** @copydoc sw_pwm_driver_set_freq */
bool sw_pwm_driver_set_freq(uint channel, float freq_hz, float duty) {
    pwm_driver_state_t state;

    if (channel >= SW_PWM_COUNT) return false;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    sw_pwm_channel_t *ch = &sw_pwm_channels[channel];
    duties[channel] = duty;

    if (freq_hz <= 0.0f) {
        uint32_t save = save_and_disable_interrupts();
        ch->active = false;
        ch->period_ticks = 1000;
        ch->duty_ticks = 500;
        ch->counter = 0;
        restore_interrupts(save);
        gpio_put(ch->gpio, 0);
        actual_freqs[channel] = 0.0f;
        enabled[channel] = false;
        state.freq_hz = actual_freqs[channel];
        state.duty = duties[channel];
        state.pulse_count = ch->pulse_count;
        pwm_driver_store_applied_state(SW_PWM_CHANNEL_BASE + channel, &state);
        return true;
    }

    float period_f = SW_PWM_BASE_HZ / freq_hz;
    if (period_f > 0xFFFFFFFF) period_f = 0xFFFFFFFF;

    uint32_t period = (uint32_t)(period_f + 0.5f);
    if (period < 1) period = 1;

    uint32_t duty_ticks = (uint32_t)(period * duty + 0.5f);
    if (duty_ticks > period) duty_ticks = period;

    uint32_t save = save_and_disable_interrupts();
    ch->period_ticks = period;
    ch->duty_ticks = duty_ticks;
    ch->counter = 0;
    ch->active = true;
    restore_interrupts(save);

    actual_freqs[channel] = SW_PWM_BASE_HZ / (float)period;
    enabled[channel] = true;
    state.freq_hz = actual_freqs[channel];
    state.duty = duties[channel];
    state.pulse_count = ch->pulse_count;
    pwm_driver_store_applied_state(SW_PWM_CHANNEL_BASE + channel, &state);

    return true;
}

/** @copydoc sw_pwm_driver_get */
bool sw_pwm_driver_get(uint channel, pwm_driver_state_t *state) {
    if (channel >= SW_PWM_COUNT || state == NULL) return false;
    state->freq_hz = actual_freqs[channel];
    state->duty = duties[channel];
    state->pulse_count = sw_pwm_channels[channel].pulse_count;
    return true;
}