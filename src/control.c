#include "control.h"
#include "hw_pwm.h"
#include "sw_pwm.h"

#define SW_CHANNEL_BASE HW_PWM_COUNT

static float freqs[CONTROL_CHANNEL_COUNT] = {0};
static float duties[CONTROL_CHANNEL_COUNT] = {0};

void control_init(void) {
    for (int i = 0; i < CONTROL_CHANNEL_COUNT; i++) {
        freqs[i] = 0.0f;
        duties[i] = 0.5f;
    }
}

bool control_set_freq(uint channel, float freq_hz) {
    if (channel >= CONTROL_CHANNEL_COUNT) return false;
    if (freq_hz < 0.0f) freq_hz = 0.0f;

    freqs[channel] = freq_hz;

    if (channel < HW_PWM_COUNT) {
        return hw_pwm_set_freq(channel, freq_hz, duties[channel]);
    } else {
        return sw_pwm_set_freq(channel - SW_CHANNEL_BASE, freq_hz, duties[channel]);
    }
}

bool control_set_duty(uint channel, float duty) {
    if (channel >= CONTROL_CHANNEL_COUNT) return false;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    duties[channel] = duty;

    if (channel < HW_PWM_COUNT) {
        hw_pwm_set_duty(channel, duty);
    } else {
        sw_pwm_set_duty(channel - SW_CHANNEL_BASE, duty);
    }
    return true;
}

float control_get_freq(uint channel) {
    if (channel >= CONTROL_CHANNEL_COUNT) return 0.0f;
    return freqs[channel];
}

float control_get_duty(uint channel) {
    if (channel >= CONTROL_CHANNEL_COUNT) return 0.0f;
    return duties[channel];
}

uint32_t control_get_pulse_count(uint channel) {
    if (channel >= CONTROL_CHANNEL_COUNT) return 0;

    if (channel < HW_PWM_COUNT) {
        return hw_pwm_get_pulse_count(channel);
    } else {
        return sw_pwm_get_pulse_count(channel - SW_CHANNEL_BASE);
    }
}

bool control_is_enabled(uint channel) {
    if (channel >= CONTROL_CHANNEL_COUNT) return false;

    if (channel < HW_PWM_COUNT) {
        return hw_pwm_is_enabled(channel);
    } else {
        return sw_pwm_is_enabled(channel - SW_CHANNEL_BASE);
    }
}

void control_stop_all(void) {
    for (int i = 0; i < CONTROL_CHANNEL_COUNT; i++) {
        freqs[i] = 0.0f;
        duties[i] = 0.5f;
        if (i < HW_PWM_COUNT) {
            hw_pwm_set_freq(i, 0.0f, 0.5f);
            hw_pwm_set_pulse_count(i, 0);
        } else {
            sw_pwm_set_freq(i - SW_CHANNEL_BASE, 0.0f, 0.5f);
            sw_pwm_set_pulse_count(i - SW_CHANNEL_BASE, 0);
        }
    }
}
