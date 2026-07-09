#ifndef PWMDRIVER_SW_PWM_DRIVER_H
#define PWMDRIVER_SW_PWM_DRIVER_H

#include "pwm_driver.h"

void sw_pwm_driver_init(void);
bool sw_pwm_driver_set_freq(uint channel, float freq_hz, float duty);
bool sw_pwm_driver_get(uint channel, pwm_driver_state_t *state);

#endif