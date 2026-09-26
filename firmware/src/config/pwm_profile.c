/**
 * @file pwm_profile.c
 * @brief Common profile lookup and validation helpers.
 */

#include "config/pwm_profile.h"

#include <stddef.h>

extern const pwm_profile_channel_t pwm_profile_channels[PWM_PROFILE_CHANNEL_COUNT];

const pwm_profile_channel_t *pwm_profile_get_channel(uint channel) {
    if (channel >= PWM_PROFILE_CHANNEL_COUNT) {
        return NULL;
    }

    return &pwm_profile_channels[channel];
}

bool pwm_profile_frequency_supported(uint channel, uint32_t frequency_hz) {
    const pwm_profile_channel_t *profile = pwm_profile_get_channel(channel);

    if (profile == NULL || frequency_hz == 0u) {
        return profile != NULL && frequency_hz == 0u;
    }

    return frequency_hz >= profile->min_frequency_hz && frequency_hz <= profile->max_frequency_hz;
}

bool pwm_profile_is_monitor(void) {
#ifdef PICO_PWM_MONITOR_PROFILE
    return true;
#else
    return false;
#endif
}

const char *pwm_profile_backend_name(pwm_profile_backend_t backend) {
    switch (backend) {
    case PWM_PROFILE_BACKEND_HW_GENERATOR:
        return "HW";
    case PWM_PROFILE_BACKEND_PIO_GENERATOR:
        return "PIO";
    case PWM_PROFILE_BACKEND_SW_GENERATOR:
        return "SW";
    case PWM_PROFILE_BACKEND_HW_MONITOR:
        return "HW-MON";
    case PWM_PROFILE_BACKEND_PIO_MONITOR:
        return "PIO-MON";
    case PWM_PROFILE_BACKEND_SW_MONITOR:
        return "SW-MON";
    default:
        return "?";
    }
}
