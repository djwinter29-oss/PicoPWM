#ifndef SW_PWM_H
#define SW_PWM_H

#include "pico/stdlib.h"

#define SW_PWM_COUNT 16

void sw_pwm_init(void);
bool sw_pwm_set_freq(uint channel, float freq_hz, float duty);
void sw_pwm_set_duty(uint channel, float duty);
void sw_pwm_enable(uint channel, bool enable);
float sw_pwm_get_freq(uint channel);
float sw_pwm_get_duty(uint channel);
bool sw_pwm_is_enabled(uint channel);

uint32_t sw_pwm_get_pulse_count(uint channel);
void sw_pwm_set_pulse_count(uint channel, uint32_t count);
void sw_pwm_reset_pulse_count(uint channel);

#endif
