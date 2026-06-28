#ifndef HW_PWM_H
#define HW_PWM_H

#include <stdint.h>
#include <stdbool.h>

#define HW_PWM_COUNT 8

void hw_pwm_init(void);
bool hw_pwm_set_freq(uint channel, float freq_hz, float duty);
void hw_pwm_set_duty(uint channel, float duty);
void hw_pwm_enable(uint channel, bool enable);
float hw_pwm_get_actual_freq(uint channel);
float hw_pwm_get_duty(uint channel);
bool hw_pwm_is_enabled(uint channel);

uint32_t hw_pwm_get_pulse_count(uint channel);
void hw_pwm_set_pulse_count(uint channel, uint32_t count);
void hw_pwm_reset_pulse_count(uint channel);

#endif
