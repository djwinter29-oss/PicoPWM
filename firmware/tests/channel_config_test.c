#include "pwmdriver/channel_config/channel_config.h"

#include <assert.h>
#include <string.h>

int main(void) {
    /* Startup roles populate the fixed channel table before the driver launches Core 1. */

    /* Configure all three banks at startup. */
    const pwm_profile_bank_role_t roles[PWM_PROFILE_BANK_COUNT] = {
        PWM_PROFILE_BANK_ROLE_GENERATOR,
        PWM_PROFILE_BANK_ROLE_MONITOR,
        PWM_PROFILE_BANK_ROLE_GENERATOR,
    };
    assert(pwm_profile_configure_roles(roles));
    {
        const pwm_profile_channel_t *profile = pwm_profile_get_channel(0u);
        assert(profile->gpio == 1u);
        assert(profile->backend == PWM_PROFILE_BACKEND_HW_GENERATOR);
        assert(profile->backend_channel == 0u);
        assert(profile->min_frequency_hz > 0u);
        assert(profile->max_frequency_hz >= profile->min_frequency_hz);
        assert(profile->accuracy_ppm > 0u);
        assert(pwm_profile_frequency_supported(0u, 0u));
        assert(pwm_profile_frequency_supported(0u, profile->min_frequency_hz));
        assert(pwm_profile_frequency_supported(0u, profile->max_frequency_hz));
        assert(!pwm_profile_frequency_supported(0u, profile->max_frequency_hz + 1u));

        uint gpio = 0u;
        assert(pwm_profile_get_gpio(profile->backend, profile->backend_channel, &gpio));
        assert(gpio == profile->gpio);

        uint channel = 0u;
        assert(pwm_profile_get_logical_channel(profile->backend, profile->backend_channel, &channel));
        assert(channel == 0u);
    }
    /* PIO bank is configured as monitor: fixed companion slice-A GPIOs, channels 8..15. */
    {
        const pwm_profile_channel_t *profile = pwm_profile_get_channel(8u);
        assert(profile->gpio == 0u);
        assert(profile->backend == PWM_PROFILE_BACKEND_PIO_MONITOR);
        assert(profile->direction == PWM_PROFILE_DIRECTION_INPUT);
    }

    /* SW bank is configured as generator: remaining GPIOs, channels 16..23. */
    {
        const pwm_profile_channel_t *profile = pwm_profile_get_channel(16u);
        assert(profile->gpio == 16u);
        assert(profile->backend == PWM_PROFILE_BACKEND_SW_GENERATOR);
    }

    assert(pwm_profile_validate());
    assert(pwm_profile_get_channel(PWM_PROFILE_CHANNEL_COUNT) == NULL);

    {
        pwm_profile_channel_t invalid[PWM_PROFILE_CHANNEL_COUNT];
        for (uint channel = 0u; channel < PWM_PROFILE_CHANNEL_COUNT; ++channel) {
            invalid[channel] = *pwm_profile_get_channel(channel);
        }

        invalid[0].backend = (pwm_profile_backend_t)99u;
        assert(!pwm_profile_validate_table(invalid, PWM_PROFILE_CHANNEL_COUNT));
        invalid[0] = *pwm_profile_get_channel(0u);

        invalid[0].direction = PWM_PROFILE_DIRECTION_DISABLED;
        invalid[0].capabilities = PWM_PROFILE_CAP_READ;
        assert(!pwm_profile_validate_table(invalid, PWM_PROFILE_CHANNEL_COUNT));
        invalid[0] = *pwm_profile_get_channel(0u);

        invalid[0].direction = PWM_PROFILE_DIRECTION_INPUT;
        invalid[0].backend = PWM_PROFILE_BACKEND_SW_MONITOR;
        invalid[0].capabilities = 0u;
        assert(!pwm_profile_validate_table(invalid, PWM_PROFILE_CHANNEL_COUNT));
        invalid[0] = *pwm_profile_get_channel(0u);

        invalid[1].gpio = invalid[0].gpio;
        assert(!pwm_profile_validate_table(invalid, PWM_PROFILE_CHANNEL_COUNT));
        invalid[1] = *pwm_profile_get_channel(1u);
        invalid[0].gpio = 23u;
        invalid[0].backend = PWM_PROFILE_BACKEND_HW_GENERATOR;
        assert(!pwm_profile_validate_table(invalid, PWM_PROFILE_CHANNEL_COUNT));
    }

    return 0;
}
