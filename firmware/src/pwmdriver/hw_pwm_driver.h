#ifndef PWMDRIVER_HW_PWM_DRIVER_H
#define PWMDRIVER_HW_PWM_DRIVER_H

#include "pwm_driver.h"

void hw_pwm_driver_init(void);
bool hw_pwm_driver_set_freq(uint channel, float freq_hz, float duty);
bool hw_pwm_driver_get(uint channel, pwm_driver_state_t *state);

#endif