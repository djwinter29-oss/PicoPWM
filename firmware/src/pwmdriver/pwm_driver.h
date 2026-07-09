#ifndef PWMDRIVER_PWM_DRIVER_H
#define PWMDRIVER_PWM_DRIVER_H

#include "pico/stdlib.h"

#define HW_PWM_COUNT 8
#define PIO_PWM_DRIVER_COUNT 8
#define SW_PWM_COUNT 8

#define HW_PWM_CHANNEL_BASE 0
#define PIO_PWM_CHANNEL_BASE (HW_PWM_CHANNEL_BASE + HW_PWM_COUNT)
#define SW_PWM_CHANNEL_BASE (PIO_PWM_CHANNEL_BASE + PIO_PWM_DRIVER_COUNT)
#define PWM_DRIVER_CHANNEL_COUNT (HW_PWM_COUNT + PIO_PWM_DRIVER_COUNT + SW_PWM_COUNT)

typedef struct {
    float freq_hz;
    float duty;
    uint32_t pulse_count;
} pwm_driver_state_t;

extern const uint PWM_HW_GPIO_PINS[];
extern const uint PWM_SW_GPIO_PINS[];
extern const uint PWM_PIO_GPIO_PINS[];

// Cross-core safe public API:
// Core 1 owns all backend driver state and hardware access.
// Calls from Core 0 enqueue mailbox commands and read the latest published
// state snapshot.
void pwm_driver_launch(void);
bool pwm_driver_is_ready(void);
bool pwm_driver_set_freq(uint channel, float freq_hz, float duty);
bool pwm_driver_get(uint channel, pwm_driver_state_t *state);

#endif