#ifndef PWMDRIVER_PWM_DRIVER_H
#define PWMDRIVER_PWM_DRIVER_H

#include "pico/stdlib.h"

#define HW_PWM_COUNT 8
#define SW_PWM_COUNT 16
#define PIO_PWM_DRIVER_COUNT 8

typedef struct {
    float freq_hz;
    float duty;
    uint32_t pulse_count;
} pwm_driver_state_t;

extern const uint PWM_HW_GPIO_PINS[];
extern const uint PWM_SW_GPIO_PINS[];
extern const uint PWM_PIO_GPIO_PINS[];

#endif