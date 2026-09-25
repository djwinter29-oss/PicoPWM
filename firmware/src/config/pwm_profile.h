/**
 * @file pwm_profile.h
 * @brief Build-selected logical channel configuration for PicoPWM.
 */

#ifndef PWM_PROFILE_H
#define PWM_PROFILE_H

#include "pico/stdlib.h"

#include <stdint.h>

typedef enum {
    PWM_PROFILE_BACKEND_HW_GENERATOR = 0,
    PWM_PROFILE_BACKEND_PIO_GENERATOR,
    PWM_PROFILE_BACKEND_SW_GENERATOR,
    PWM_PROFILE_BACKEND_HW_MONITOR,
    PWM_PROFILE_BACKEND_PIO_MONITOR,
    PWM_PROFILE_BACKEND_SW_MONITOR,
} pwm_profile_backend_t;

typedef enum {
    PWM_PROFILE_DIRECTION_OUTPUT = 0,
    PWM_PROFILE_DIRECTION_INPUT,
    PWM_PROFILE_DIRECTION_DISABLED,
} pwm_profile_direction_t;

typedef enum {
    PWM_PROFILE_CAP_READ = 1u << 0,
    PWM_PROFILE_CAP_SET = 1u << 1,
} pwm_profile_capability_t;

typedef struct {
    pwm_profile_backend_t backend;
    pwm_profile_direction_t direction;
    uint gpio;
    uint backend_channel;
    uint8_t capabilities;
} pwm_profile_channel_t;

/** @brief Number of logical channels exposed by the selected profile. */
#define PWM_PROFILE_CHANNEL_COUNT 24u

/** @brief Return the active profile's logical channel table. */
const pwm_profile_channel_t *pwm_profile_get_channel(uint channel);

/** @brief Return a short name for one configured backend. */
const char *pwm_profile_backend_name(pwm_profile_backend_t backend);

#endif
