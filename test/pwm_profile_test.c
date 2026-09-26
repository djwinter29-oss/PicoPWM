#include "profile/pwm_profile.h"

#include <assert.h>
#include <string.h>

int main(void) {
    const pwm_profile_channel_t *profile = pwm_profile_get_channel(0u);

    assert(profile != NULL);
    assert(pwm_profile_validate());
#ifdef PICO_PWM_MIXED_PROFILE
    assert(profile->gpio == 0u);
    assert(profile->backend == PWM_PROFILE_BACKEND_PIO_GENERATOR);
#elif defined(PICO_PWM_SOFTWARE_PROFILE)
    assert(profile->gpio == 0u);
#ifdef PICO_PWM_MONITOR_PROFILE
    assert(profile->backend == PWM_PROFILE_BACKEND_SW_MONITOR);
#else
    assert(profile->backend == PWM_PROFILE_BACKEND_SW_GENERATOR);
#endif
#else
    assert(profile->gpio == 1u);
#endif
    assert(profile->backend_channel == 0u);
    assert(profile->min_frequency_hz > 0u);
    assert(profile->max_frequency_hz >= profile->min_frequency_hz);
    assert(profile->accuracy_ppm > 0u);
    assert(pwm_profile_frequency_supported(0u, 0u));
    assert(pwm_profile_frequency_supported(0u, profile->min_frequency_hz));
    assert(pwm_profile_frequency_supported(0u, profile->max_frequency_hz));
    assert(!pwm_profile_frequency_supported(0u, profile->max_frequency_hz + 1u));
    {
        uint gpio = 0u;
        assert(pwm_profile_get_gpio(profile->backend, profile->backend_channel, &gpio));
        assert(gpio == profile->gpio);
    }
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
