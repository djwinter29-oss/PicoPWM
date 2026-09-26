/**
 * @file pwm_profile.c
 * @brief Common profile lookup and validation helpers.
 */

#include "profile/pwm_profile.h"

#include "hardware/pwm.h"

#include <stddef.h>

extern const pwm_profile_channel_t pwm_profile_channels[PWM_PROFILE_CHANNEL_COUNT];

const pwm_profile_channel_t *pwm_profile_get_channel(uint channel) {
    if (channel >= PWM_PROFILE_CHANNEL_COUNT) {
        return NULL;
    }

    return &pwm_profile_channels[channel];
}

bool pwm_profile_get_gpio(pwm_profile_backend_t backend, uint backend_channel, uint *gpio_out) {
    if (gpio_out == NULL) {
        return false;
    }

    for (uint channel = 0u; channel < PWM_PROFILE_CHANNEL_COUNT; ++channel) {
        const pwm_profile_channel_t *profile = &pwm_profile_channels[channel];
        if (profile->backend == backend && profile->backend_channel == backend_channel) {
            *gpio_out = profile->gpio;
            return true;
        }
    }

    return false;
}

bool pwm_profile_get_logical_channel(pwm_profile_backend_t backend, uint backend_channel, uint *channel_out) {
    if (channel_out == NULL) {
        return false;
    }

    for (uint channel = 0u; channel < PWM_PROFILE_CHANNEL_COUNT; ++channel) {
        const pwm_profile_channel_t *profile = &pwm_profile_channels[channel];
        if (profile->backend == backend && profile->backend_channel == backend_channel) {
            *channel_out = channel;
            return true;
        }
    }

    return false;
}

uint pwm_profile_backend_channel_count(pwm_profile_backend_t backend) {
    uint count = 0u;

    for (uint channel = 0u; channel < PWM_PROFILE_CHANNEL_COUNT; ++channel) {
        count += pwm_profile_channels[channel].backend == backend ? 1u : 0u;
    }

    return count;
}

bool pwm_profile_validate(void) {
    for (uint channel = 0u; channel < PWM_PROFILE_CHANNEL_COUNT; ++channel) {
        const pwm_profile_channel_t *profile = &pwm_profile_channels[channel];

        if (profile->gpio >= 30u) {
            return false;
        }
        if (profile->max_frequency_hz < profile->min_frequency_hz || profile->accuracy_ppm == 0u) {
            return false;
        }
        if ((profile->direction == PWM_PROFILE_DIRECTION_OUTPUT) &&
            (profile->backend >= PWM_PROFILE_BACKEND_HW_MONITOR ||
             (profile->capabilities & PWM_PROFILE_CAP_SET) == 0u)) {
            return false;
        }
        if ((profile->direction == PWM_PROFILE_DIRECTION_INPUT) &&
            (profile->backend < PWM_PROFILE_BACKEND_HW_MONITOR ||
             (profile->capabilities & PWM_PROFILE_CAP_SET) != 0u)) {
            return false;
        }
        if ((profile->backend == PWM_PROFILE_BACKEND_HW_GENERATOR ||
             profile->backend == PWM_PROFILE_BACKEND_HW_MONITOR) &&
            ((profile->backend_channel >= 8u) ||
             (pwm_gpio_to_channel(profile->gpio) != PWM_CHAN_B))) {
            return false;
        }
        if ((profile->backend == PWM_PROFILE_BACKEND_PIO_GENERATOR ||
             profile->backend == PWM_PROFILE_BACKEND_PIO_MONITOR) &&
            profile->backend_channel >= 8u) {
            return false;
        }

        for (uint other = channel + 1u; other < PWM_PROFILE_CHANNEL_COUNT; ++other) {
            const pwm_profile_channel_t *candidate = &pwm_profile_channels[other];
            if (candidate->gpio == profile->gpio) {
                return false;
            }
            if (candidate->backend == profile->backend && candidate->backend_channel == profile->backend_channel) {
                return false;
            }
        }
    }

    return true;
}

bool pwm_profile_frequency_supported(uint channel, uint32_t frequency_hz) {
    const pwm_profile_channel_t *profile = pwm_profile_get_channel(channel);

    if (profile == NULL || frequency_hz == 0u) {
        return profile != NULL && frequency_hz == 0u;
    }

    return frequency_hz >= profile->min_frequency_hz && frequency_hz <= profile->max_frequency_hz;
}

bool pwm_profile_is_monitor(void) {
    return pwm_profile_backend_channel_count(PWM_PROFILE_BACKEND_HW_MONITOR) != 0u ||
           pwm_profile_backend_channel_count(PWM_PROFILE_BACKEND_PIO_MONITOR) != 0u ||
           pwm_profile_backend_channel_count(PWM_PROFILE_BACKEND_SW_MONITOR) != 0u;
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
