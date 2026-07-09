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

typedef enum {
    PWM_DRIVER_RESULT_OK = 0,
    PWM_DRIVER_RESULT_BUSY,
    PWM_DRIVER_RESULT_INVALID,
    PWM_DRIVER_RESULT_UNAVAILABLE,
    PWM_DRIVER_RESULT_TIMEOUT,
    PWM_DRIVER_RESULT_APPLY_FAILED,
} pwm_driver_result_t;

typedef struct {
    float freq_hz;
    float duty;
    uint32_t pulse_count;
} pwm_driver_state_t;

pwm_driver_result_t control_set(uint channel, float freq_hz, float duty);
pwm_driver_result_t control_set_freq(uint channel, float freq_hz);
pwm_driver_result_t control_set_duty(uint channel, float duty);
bool control_get(uint channel, pwm_driver_state_t *state);
float control_get_freq(uint channel);
float control_get_duty(uint channel);
uint32_t control_get_pulse_count(uint channel);
bool control_is_enabled(uint channel);
pwm_driver_result_t control_stop_all(void);

// Cross-core safe public API:
// Core 1 owns all backend driver state and hardware access.
// Calls from Core 0 serialize at the public write boundary, enqueue mailbox
// commands, wait for Core 1 to apply them, and read the latest published state
// snapshot. pwm_driver_set_freq() is a Core 0 command-ingress API and returns
// explicit busy/invalid/failure status.
// Higher command layers should normally prefer control_set() for full-state
// writes and use the other control_* helpers for read-modify-write updates.
void pwm_driver_launch(void);
bool pwm_driver_is_ready(void);
pwm_driver_result_t pwm_driver_set_freq(uint channel, float freq_hz, float duty);
bool pwm_driver_get(uint channel, pwm_driver_state_t *state);

#endif