#ifndef PWMDRIVER_PWM_DRIVER_INTERNAL_H
#define PWMDRIVER_PWM_DRIVER_INTERNAL_H

#include "pwm_driver.h"

extern const uint PWM_HW_GPIO_PINS[];
extern const uint PWM_SW_GPIO_PINS[];
extern const uint PWM_PIO_GPIO_PINS[];

void pwm_driver_store_applied_state(uint channel, const pwm_driver_state_t *state);
void pwm_driver_store_pulse_count(uint channel, uint32_t pulse_count);

#endif