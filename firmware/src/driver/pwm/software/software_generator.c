/**
 * @file generator.c
 * @brief Software PWM generator backend implementation for the logical `pwm` layer.
 */

#include "software_generator.h"

#include "../pwm_driver.h"
#include "../pwm_driver_internal.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/time.h"
#include "../pwm_math.h"

/** @brief Software PWM scheduler tick period in microseconds. */
#define SW_PWM_TICK_US 10
/** @brief Base tick frequency derived from @ref SW_PWM_TICK_US. */
#define SW_PWM_BASE_HZ 100000u

/** @brief Runtime scheduling state for one software PWM backend channel. */
typedef struct {
    uint gpio;             /**< Assigned output GPIO. */
    uint32_t period_ticks; /**< Whole-tick PWM period for the current configuration. */
    uint32_t duty_ticks;   /**< Whole-tick active window for the current configuration. */
    uint32_t counter;      /**< Current tick position within the PWM period. */
} sw_pwm_channel_t;

/** @brief Software PWM runtime state in backend-local channel order. */
static sw_pwm_channel_t sw_pwm_channels[PWM_DRIVER_CONFIG_BANK_SIZE];
/**
 * @brief Private alarm pool created on Core 1, so the tick interrupt runs on Core 1 with the rest of the backend.
 *
 * The default pool belongs to Core 0 and would run the 100 kHz tick there, racing the Core 1 writers below.
 */
static alarm_pool_t *sw_pwm_pool;
/** @brief Repeating timer that drives the software PWM scheduler; armed only while a channel is active. */
static repeating_timer_t sw_pwm_timer;
/** @brief True while @ref sw_pwm_timer is armed; touched only on Core 1 with interrupts masked or in the tick. */
static bool sw_pwm_timer_running = false;
/** @brief Bitmask of backend-local software PWM channels currently driven by the scheduler. */
static uint32_t sw_pwm_active_mask = 0u;

/** @brief Publish one channel's realized software PWM state into the shared driver snapshot. */
static void sw_gen_publish_state(uint channel, uint32_t freq_hz, uint8_t duty) {
    pwm_state_t state = {.freq_hz = freq_hz, .duty = duty};

    pwm_driver_store_applied_state((uint)PWM_BANK_C * PWM_DRIVER_CONFIG_BANK_SIZE + channel, &state);
}

/** @brief Repeating timer callback that advances all active channels and disarms itself when none remain. */
static bool sw_pwm_tick_callback(repeating_timer_t *rt) {
    uint32_t active_mask = sw_pwm_active_mask;

    (void)rt;

    if (active_mask == 0u) {
        sw_pwm_timer_running = false;
        return false;
    }

    while (active_mask != 0u) {
        uint32_t channel = (uint32_t)__builtin_ctz(active_mask);
        sw_pwm_channel_t *ch = &sw_pwm_channels[channel];

        active_mask &= active_mask - 1u;

        ch->counter++;
        if (ch->counter >= ch->period_ticks) {
            ch->counter = 0;
        }
        gpio_put(ch->gpio, ch->counter < ch->duty_ticks);
    }

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
    restore_interrupts(save);

    gpio_put(ch->gpio, high);
    sw_gen_publish_state(channel, 0u, realized_duty);
}

/**
 * @brief Arm one channel for scheduled output, starting the tick when this is the first active channel.
 * @return `false` when the tick could not be armed; the channel is left inactive.
 */
static bool sw_gen_start_scheduled_output(uint channel, uint32_t period_ticks, uint32_t duty_ticks) {
    sw_pwm_channel_t *ch = &sw_pwm_channels[channel];
    uint32_t save = save_and_disable_interrupts();
    bool arm;

    ch->period_ticks = period_ticks;
    ch->duty_ticks = duty_ticks;
    ch->counter = 0u;
    sw_pwm_active_mask |= (1u << channel);
    arm = !sw_pwm_timer_running;
    sw_pwm_timer_running = true;
    restore_interrupts(save);

    if (arm &&
        !alarm_pool_add_repeating_timer_us(sw_pwm_pool, -SW_PWM_TICK_US, sw_pwm_tick_callback, NULL, &sw_pwm_timer)) {
        save = save_and_disable_interrupts();
        sw_pwm_active_mask &= ~(1u << channel);
        sw_pwm_timer_running = false;
        restore_interrupts(save);
        return false;
    }

    return true;
}

/** @copydoc sw_gen_init */
bool sw_gen_init(void) {
    sw_pwm_active_mask = 0u;

    if (sw_pwm_pool == NULL) {
        sw_pwm_pool = alarm_pool_create_with_unused_hardware_alarm(1);
        if (sw_pwm_pool == NULL) {
            return false;
        }
    }
    // The tick must run on the core that calls sw_gen_init; fail here rather than race silently.
    if (alarm_pool_core_num(sw_pwm_pool) != get_core_num()) {
        return false;
    }

    for (uint i = 0u; i < PWM_DRIVER_CONFIG_BANK_SIZE; i++) {
        uint gpio = software_pwm_config.channels[i].gpio;

        sw_pwm_channels[i] = (sw_pwm_channel_t){.gpio = gpio};
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, 0);
    }

    return true;
}

/** @copydoc sw_gen_set */
bool sw_gen_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    uint8_t realized_duty;
    uint32_t period;
    uint32_t duty_ticks;
    bool high;

    if (channel >= PWM_DRIVER_CONFIG_BANK_SIZE) {
        return false;
    }
    if (duty > 100u) {
        duty = 100u;
    }

    if (pwm_generator_resolve_static(freq_hz, duty, &high, &realized_duty)) {
        sw_gen_drive_static_level(channel, realized_duty, high);
        return true;
    }

    if (freq_hz > SW_GEN_MAX_FREQ_HZ) {
        return false;
    }

    period = (SW_PWM_BASE_HZ + (freq_hz / 2u)) / freq_hz;
    if (period < 1u) {
        period = 1u;
    }
    duty_ticks = (uint32_t)(((uint64_t)period * duty + 50u) / 100u);
    if (duty_ticks > period) {
        duty_ticks = period;
    }

    if (!sw_gen_start_scheduled_output(channel, period, duty_ticks)) {
        return false;
    }
    sw_gen_publish_state(channel, (SW_PWM_BASE_HZ + (period / 2u)) / period, pwm_realized_duty(duty_ticks, period));
    return true;
}
