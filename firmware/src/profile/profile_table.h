#ifndef PWM_PROFILE_TABLE_H
#define PWM_PROFILE_TABLE_H

#include "profile/pwm_profile.h"

#define PWM_PROFILE_HW_GENERATOR_CHANNEL(gpio_value, local) \
    {.backend = PWM_PROFILE_BACKEND_HW_GENERATOR, .direction = PWM_PROFILE_DIRECTION_OUTPUT, .gpio = (gpio_value), .backend_channel = (local), .capabilities = PWM_PROFILE_CAP_READ | PWM_PROFILE_CAP_SET, .min_frequency_hz = 10u, .max_frequency_hz = 1000000u, .accuracy_ppm = 100u}
#define PWM_PROFILE_PIO_GENERATOR_CHANNEL(gpio_value, local) \
    {.backend = PWM_PROFILE_BACKEND_PIO_GENERATOR, .direction = PWM_PROFILE_DIRECTION_OUTPUT, .gpio = (gpio_value), .backend_channel = (local), .capabilities = PWM_PROFILE_CAP_READ | PWM_PROFILE_CAP_SET, .min_frequency_hz = 1u, .max_frequency_hz = 1000000u, .accuracy_ppm = 1000u}
#define PWM_PROFILE_SW_GENERATOR_CHANNEL(gpio_value, local) \
    {.backend = PWM_PROFILE_BACKEND_SW_GENERATOR, .direction = PWM_PROFILE_DIRECTION_OUTPUT, .gpio = (gpio_value), .backend_channel = (local), .capabilities = PWM_PROFILE_CAP_READ | PWM_PROFILE_CAP_SET, .min_frequency_hz = 1u, .max_frequency_hz = 1000u, .accuracy_ppm = 10000u}
#define PWM_PROFILE_HW_MONITOR_CHANNEL(gpio_value, local) \
    {.backend = PWM_PROFILE_BACKEND_HW_MONITOR, .direction = PWM_PROFILE_DIRECTION_INPUT, .gpio = (gpio_value), .backend_channel = (local), .capabilities = PWM_PROFILE_CAP_READ, .min_frequency_hz = 1u, .max_frequency_hz = 1000u, .accuracy_ppm = 10000u}
#define PWM_PROFILE_PIO_MONITOR_CHANNEL(gpio_value, local) \
    {.backend = PWM_PROFILE_BACKEND_PIO_MONITOR, .direction = PWM_PROFILE_DIRECTION_INPUT, .gpio = (gpio_value), .backend_channel = (local), .capabilities = PWM_PROFILE_CAP_READ, .min_frequency_hz = 1u, .max_frequency_hz = 1000000u, .accuracy_ppm = 10000u}
#define PWM_PROFILE_SW_MONITOR_CHANNEL(gpio_value, local) \
    {.backend = PWM_PROFILE_BACKEND_SW_MONITOR, .direction = PWM_PROFILE_DIRECTION_INPUT, .gpio = (gpio_value), .backend_channel = (local), .capabilities = PWM_PROFILE_CAP_READ, .min_frequency_hz = 1u, .max_frequency_hz = 1000u, .accuracy_ppm = 10000u}

#endif
