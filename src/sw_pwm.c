#include "sw_pwm.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

// GPIO pins for 16 software PWM channels.
// These are the pins left after allocating 8 HW PWM (slices A) and UART1 (GPIO16/17).
static const uint sw_pwm_gpios[SW_PWM_COUNT] = {
    1, 3, 5, 7, 9, 11, 13, 15,
    18, 19, 20, 21, 22, 25, 26, 27
};

// 10 us tick -> 100 kHz base rate.
// For 1 kHz PWM, this gives 100 levels of resolution (1 % step).
#define SW_PWM_TICK_US 10
#define SW_PWM_BASE_HZ 100000.0f

typedef struct {
    uint gpio;
    uint32_t period_ticks;
    uint32_t duty_ticks;
    uint32_t counter;
    uint32_t pulse_count;
    bool active;
} sw_pwm_channel_t;

static sw_pwm_channel_t sw_pwm_channels[SW_PWM_COUNT];
static repeating_timer_t sw_pwm_timer;

static float requested_freqs[SW_PWM_COUNT] = {0};
static float duties[SW_PWM_COUNT] = {0};
static bool enabled[SW_PWM_COUNT] = {false};

// Called from timer interrupt context at 100 kHz.
static bool sw_pwm_tick_callback(repeating_timer_t *rt) {
    (void)rt;

    for (int i = 0; i < SW_PWM_COUNT; i++) {
        sw_pwm_channel_t *ch = &sw_pwm_channels[i];
        if (!ch->active) continue;

        ch->counter++;
        if (ch->counter >= ch->period_ticks) {
            ch->counter = 0;
            ch->pulse_count++;
        }
        gpio_put(ch->gpio, ch->counter < ch->duty_ticks);
    }

    return true;
}

void sw_pwm_init(void) {
    for (int i = 0; i < SW_PWM_COUNT; i++) {
        uint gpio = sw_pwm_gpios[i];

        sw_pwm_channels[i].gpio = gpio;
        sw_pwm_channels[i].period_ticks = 1000;
        sw_pwm_channels[i].duty_ticks = 500;
        sw_pwm_channels[i].counter = 0;
        sw_pwm_channels[i].pulse_count = 0;
        sw_pwm_channels[i].active = false;

        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, 0);

        requested_freqs[i] = 0.0f;
        duties[i] = 0.5f;
        enabled[i] = false;
    }

    // Start periodic 10 us timer (negative value = repeat relative).
    add_repeating_timer_us(-SW_PWM_TICK_US, sw_pwm_tick_callback, NULL, &sw_pwm_timer);
}

bool sw_pwm_set_freq(uint channel, float freq_hz, float duty) {
    if (channel >= SW_PWM_COUNT) return false;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    sw_pwm_channel_t *ch = &sw_pwm_channels[channel];
    duties[channel] = duty;

    if (freq_hz <= 0.0f) {
        // freq = 0 means disable.
        ch->active = false;
        ch->period_ticks = 1000;
        ch->duty_ticks = 500;
        ch->counter = 0;
        requested_freqs[channel] = 0.0f;
        enabled[channel] = false;
        gpio_put(ch->gpio, 0);
        return true;
    }

    float period_f = SW_PWM_BASE_HZ / freq_hz;
    if (period_f > 0xFFFFFFFF) period_f = 0xFFFFFFFF;

    uint32_t period = (uint32_t)(period_f + 0.5f);
    if (period < 1) period = 1;

    uint32_t duty_ticks = (uint32_t)(period * duty + 0.5f);
    if (duty_ticks > period) duty_ticks = period;

    ch->period_ticks = period;
    ch->duty_ticks = duty_ticks;
    ch->counter = 0;
    ch->active = true;

    requested_freqs[channel] = freq_hz;
    enabled[channel] = true;

    return true;
}

void sw_pwm_set_duty(uint channel, float duty) {
    if (channel >= SW_PWM_COUNT) return;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    sw_pwm_channel_t *ch = &sw_pwm_channels[channel];
    ch->duty_ticks = (uint32_t)(ch->period_ticks * duty + 0.5f);
    if (ch->duty_ticks > ch->period_ticks) ch->duty_ticks = ch->period_ticks;
    duties[channel] = duty;
}

void sw_pwm_enable(uint channel, bool enable) {
    if (channel >= SW_PWM_COUNT) return;

    enabled[channel] = enable;
    sw_pwm_channels[channel].active = enable;
    if (!enable) {
        gpio_put(sw_pwm_channels[channel].gpio, 0);
    }
}

float sw_pwm_get_freq(uint channel) {
    if (channel >= SW_PWM_COUNT) return 0.0f;
    return requested_freqs[channel];
}

float sw_pwm_get_duty(uint channel) {
    if (channel >= SW_PWM_COUNT) return 0.0f;
    return duties[channel];
}

bool sw_pwm_is_enabled(uint channel) {
    if (channel >= SW_PWM_COUNT) return false;
    return enabled[channel];
}

uint32_t sw_pwm_get_pulse_count(uint channel) {
    if (channel >= SW_PWM_COUNT) return 0;
    return sw_pwm_channels[channel].pulse_count;
}

void sw_pwm_set_pulse_count(uint channel, uint32_t count) {
    if (channel >= SW_PWM_COUNT) return;
    sw_pwm_channels[channel].pulse_count = count;
}

void sw_pwm_reset_pulse_count(uint channel) {
    sw_pwm_set_pulse_count(channel, 0);
}
