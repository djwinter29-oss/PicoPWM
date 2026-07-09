#include "hw_pwm.h"

#include "pwmdriver/hw_pwm_driver.h"

void hw_pwm_init(void) {
    hw_pwm_driver_init();
}

bool hw_pwm_set_freq(uint channel, float freq_hz, float duty) {
    return hw_pwm_driver_set_freq(channel, freq_hz, duty);
}

void hw_pwm_set_duty(uint channel, float duty) {
    hw_pwm_driver_set_duty(channel, duty);
}

void hw_pwm_enable(uint channel, bool enable) {
    hw_pwm_driver_enable(channel, enable);
}

float hw_pwm_get_actual_freq(uint channel) {
    return hw_pwm_driver_get_actual_freq(channel);
}

float hw_pwm_get_duty(uint channel) {
    return hw_pwm_driver_get_duty(channel);
}

bool hw_pwm_is_enabled(uint channel) {
    return hw_pwm_driver_is_enabled(channel);
}

uint32_t hw_pwm_get_pulse_count(uint channel) {
    return hw_pwm_driver_get_pulse_count(channel);
}

void hw_pwm_set_pulse_count(uint channel, uint32_t count) {
    hw_pwm_driver_set_pulse_count(channel, count);
}

void hw_pwm_reset_pulse_count(uint channel) {
    hw_pwm_driver_reset_pulse_count(channel);
}
