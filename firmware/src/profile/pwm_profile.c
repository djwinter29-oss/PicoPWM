/**
 * @file pwm_profile.c
 * @brief Common profile lookup, validation, and runtime bank-locking helpers.
 */

#include "profile/pwm_profile.h"
#include "profile/profile_table.h"

#include "hardware/pwm.h"

#include <stddef.h>

/**
 * @brief Runtime logical channel table.
 *
 * All channels start `DISABLED` at boot. Each bank's 8 channels are filled in
 * exactly once, by `pwm_profile_lock_bank()`, when the host first requests
 * that bank's role.
 */
static pwm_profile_channel_t pwm_profile_channels[PWM_PROFILE_CHANNEL_COUNT] = {
    [0 ... PWM_PROFILE_CHANNEL_COUNT - 1] = {.direction = PWM_PROFILE_DIRECTION_DISABLED},
};

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
        if (profile->direction != PWM_PROFILE_DIRECTION_DISABLED &&
            profile->backend == backend && profile->backend_channel == backend_channel) {
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
        if (profile->direction != PWM_PROFILE_DIRECTION_DISABLED &&
            profile->backend == backend && profile->backend_channel == backend_channel) {
            *channel_out = channel;
            return true;
        }
    }

    return false;
}

uint pwm_profile_backend_channel_count(pwm_profile_backend_t backend) {
    uint count = 0u;

    for (uint channel = 0u; channel < PWM_PROFILE_CHANNEL_COUNT; ++channel) {
        count += (pwm_profile_channels[channel].direction != PWM_PROFILE_DIRECTION_DISABLED &&
              pwm_profile_channels[channel].backend == backend) ? 1u : 0u;
    }

    return count;
}

bool pwm_profile_validate_table(const pwm_profile_channel_t *channels, uint count) {
    if (channels == NULL || count != PWM_PROFILE_CHANNEL_COUNT) {
        return false;
    }

    for (uint channel = 0u; channel < count; ++channel) {
        const pwm_profile_channel_t *profile = &channels[channel];

        if (profile->backend > PWM_PROFILE_BACKEND_SW_MONITOR) {
            return false;
        }
        if (profile->direction == PWM_PROFILE_DIRECTION_DISABLED) {
            if (profile->capabilities != 0u) {
                return false;
            }
            continue;
        }
        if (profile->gpio >= PWM_PROFILE_GPIO_COUNT ||
            (profile->gpio < 32u && (PWM_PROFILE_RESERVED_GPIO_MASK & (1u << profile->gpio)) != 0u)) {
            return false;
        }
        if ((profile->gpio == 23u || profile->gpio == 24u) &&
            (profile->backend != PWM_PROFILE_BACKEND_SW_GENERATOR &&
             profile->backend != PWM_PROFILE_BACKEND_SW_MONITOR)) {
            return false;
        }
        if (profile->max_frequency_hz < profile->min_frequency_hz || profile->accuracy_ppm == 0u) {
            return false;
        }
        if ((profile->direction == PWM_PROFILE_DIRECTION_OUTPUT) &&
            (profile->backend >= PWM_PROFILE_BACKEND_HW_MONITOR ||
             (profile->capabilities & (PWM_PROFILE_CAP_READ | PWM_PROFILE_CAP_SET)) !=
                 (PWM_PROFILE_CAP_READ | PWM_PROFILE_CAP_SET))) {
            return false;
        }
        if ((profile->direction == PWM_PROFILE_DIRECTION_INPUT) &&
            (profile->backend < PWM_PROFILE_BACKEND_HW_MONITOR ||
             (profile->capabilities & PWM_PROFILE_CAP_SET) != 0u ||
             (profile->capabilities & PWM_PROFILE_CAP_READ) == 0u)) {
            return false;
        }
        if ((profile->backend == PWM_PROFILE_BACKEND_HW_GENERATOR ||
             profile->backend == PWM_PROFILE_BACKEND_HW_MONITOR) &&
            ((profile->backend_channel >= 8u) ||
             (PWM_PROFILE_REQUIRE_HW_CHANNEL_B && pwm_gpio_to_channel(profile->gpio) != PWM_CHAN_B))) {
            return false;
        }
        if ((profile->backend == PWM_PROFILE_BACKEND_PIO_GENERATOR ||
             profile->backend == PWM_PROFILE_BACKEND_PIO_MONITOR) &&
            profile->backend_channel >= 8u) {
            return false;
        }

        for (uint other = channel + 1u; other < count; ++other) {
            const pwm_profile_channel_t *candidate = &channels[other];
            if (candidate->direction != PWM_PROFILE_DIRECTION_DISABLED && candidate->gpio == profile->gpio) {
                return false;
            }
            if (candidate->direction != PWM_PROFILE_DIRECTION_DISABLED &&
                candidate->backend == profile->backend && candidate->backend_channel == profile->backend_channel) {
                return false;
            }
        }
    }

    return true;
}

bool pwm_profile_validate(void) {
    return pwm_profile_validate_table(pwm_profile_channels, PWM_PROFILE_CHANNEL_COUNT);
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

pwm_profile_backend_t pwm_profile_bank_backend(pwm_profile_bank_t bank, pwm_profile_bank_role_t role) {
    bool generator = (role == PWM_PROFILE_BANK_ROLE_GENERATOR);

    switch (bank) {
    case PWM_PROFILE_BANK_HW:
        return generator ? PWM_PROFILE_BACKEND_HW_GENERATOR : PWM_PROFILE_BACKEND_HW_MONITOR;
    case PWM_PROFILE_BANK_PIO:
        return generator ? PWM_PROFILE_BACKEND_PIO_GENERATOR : PWM_PROFILE_BACKEND_PIO_MONITOR;
    case PWM_PROFILE_BANK_SW:
    default:
        return generator ? PWM_PROFILE_BACKEND_SW_GENERATOR : PWM_PROFILE_BACKEND_SW_MONITOR;
    }
}

pwm_profile_bank_state_t pwm_profile_get_bank_state(pwm_profile_bank_t bank) {
    uint base;

    if (bank >= PWM_PROFILE_BANK_COUNT) {
        return PWM_PROFILE_BANK_STATE_UNLOCKED;
    }

    base = (uint)bank * PWM_PROFILE_BANK_SIZE;
    if (pwm_profile_channels[base].direction == PWM_PROFILE_DIRECTION_DISABLED) {
        return PWM_PROFILE_BANK_STATE_UNLOCKED;
    }

    return (pwm_profile_channels[base].backend == pwm_profile_bank_backend(bank, PWM_PROFILE_BANK_ROLE_GENERATOR))
               ? PWM_PROFILE_BANK_STATE_GENERATOR
               : PWM_PROFILE_BANK_STATE_MONITOR;
}

/** @brief Fixed GPIO assignment for each bank's 8 logical channels; see docs/pinout.md. */
static const uint pwm_profile_bank_gpio[PWM_PROFILE_BANK_COUNT][PWM_PROFILE_BANK_SIZE] = {
    [PWM_PROFILE_BANK_HW] = {1u, 3u, 5u, 7u, 9u, 11u, 13u, 15u},
    [PWM_PROFILE_BANK_PIO] = {0u, 2u, 4u, 6u, 8u, 10u, 12u, 14u},
    [PWM_PROFILE_BANK_SW] = {16u, 17u, 18u, 19u, 20u, 21u, 22u, 28u},
};

/** @brief Fill one bank's 8 channel entries for the resolved backend and role. */
static void pwm_profile_fill_bank(pwm_profile_bank_t bank, pwm_profile_bank_role_t role) {
    uint base = (uint)bank * PWM_PROFILE_BANK_SIZE;
    bool generator = (role == PWM_PROFILE_BANK_ROLE_GENERATOR);

    for (uint i = 0u; i < PWM_PROFILE_BANK_SIZE; ++i) {
        uint gpio = pwm_profile_bank_gpio[bank][i];

        switch (bank) {
        case PWM_PROFILE_BANK_HW:
            pwm_profile_channels[base + i] = generator ? (pwm_profile_channel_t)PWM_PROFILE_HW_GENERATOR_CHANNEL(gpio, i)
                                                        : (pwm_profile_channel_t)PWM_PROFILE_HW_MONITOR_CHANNEL(gpio, i);
            break;
        case PWM_PROFILE_BANK_PIO:
            pwm_profile_channels[base + i] = generator ? (pwm_profile_channel_t)PWM_PROFILE_PIO_GENERATOR_CHANNEL(gpio, i)
                                                        : (pwm_profile_channel_t)PWM_PROFILE_PIO_MONITOR_CHANNEL(gpio, i);
            break;
        case PWM_PROFILE_BANK_SW:
        default:
            pwm_profile_channels[base + i] = generator ? (pwm_profile_channel_t)PWM_PROFILE_SW_GENERATOR_CHANNEL(gpio, i)
                                                        : (pwm_profile_channel_t)PWM_PROFILE_SW_MONITOR_CHANNEL(gpio, i);
            break;
        }
    }
}

bool pwm_profile_lock_bank(pwm_profile_bank_t bank, pwm_profile_bank_role_t role) {
    if ((bank >= PWM_PROFILE_BANK_COUNT) ||
        (role != PWM_PROFILE_BANK_ROLE_GENERATOR && role != PWM_PROFILE_BANK_ROLE_MONITOR)) {
        return false;
    }

    if (pwm_profile_get_bank_state(bank) != PWM_PROFILE_BANK_STATE_UNLOCKED) {
        return false;
    }

    /* ponytail: this 8-entry fill is not synchronized against a concurrent Core 0 reader of
     * this same bank's channels. Acceptable because entries only ever move once, monotonically,
     * from all-disabled to fully valid (never live-reconfigured); a reader observing a
     * mid-fill state sees either the safe disabled behavior or the final valid one. If banks
     * ever become re-lockable without a reboot, add a critical section here. */
    pwm_profile_fill_bank(bank, role);

    return true;
}
