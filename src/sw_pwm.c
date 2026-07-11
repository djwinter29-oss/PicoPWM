#include "sw_pwm.h"

#include "pwmdriver/sw/generator.h"

void sw_pwm_init(void) {
    sw_pwm_driver_init();
}

bool sw_pwm_set_freq(uint channel, float freq_hz, float duty) {
    return sw_pwm_driver_set_freq(channel, freq_hz, duty);
}

void sw_pwm_set_duty(uint channel, float duty) {
    sw_pwm_driver_set_duty(channel, duty);
}

void sw_pwm_enable(uint channel, bool enable) {
    sw_pwm_driver_enable(channel, enable);
}

float sw_pwm_get_freq(uint channel) {
    return sw_pwm_driver_get_freq(channel);
}

float sw_pwm_get_duty(uint channel) {
    return sw_pwm_driver_get_duty(channel);
}

bool sw_pwm_is_enabled(uint channel) {
    return sw_pwm_driver_is_enabled(channel);
}

uint32_t sw_pwm_get_pulse_count(uint channel) {
    return sw_pwm_driver_get_pulse_count(channel);
}

void sw_pwm_set_pulse_count(uint channel, uint32_t count) {
    sw_pwm_driver_set_pulse_count(channel, count);
}

void sw_pwm_reset_pulse_count(uint channel) {
    sw_pwm_driver_reset_pulse_count(channel);
}
