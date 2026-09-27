#include "channel_config/pwm_profile.h"

#include <assert.h>
#include <string.h>

int main(void) {
    /* Boot state: every bank starts unlocked and the table is trivially valid. */
    assert(pwm_profile_validate());
    assert(pwm_profile_get_bank_state(PWM_PROFILE_BANK_HW) == PWM_PROFILE_BANK_STATE_UNLOCKED);
    assert(pwm_profile_get_bank_state(PWM_PROFILE_BANK_PIO) == PWM_PROFILE_BANK_STATE_UNLOCKED);
    assert(pwm_profile_get_bank_state(PWM_PROFILE_BANK_SW) == PWM_PROFILE_BANK_STATE_UNLOCKED);
    assert(pwm_profile_get_channel(0u)->direction == PWM_PROFILE_DIRECTION_DISABLED);

    /* Invalid lock requests are rejected without changing anything. */
    assert(!pwm_profile_lock_bank((pwm_profile_bank_t)99u, PWM_PROFILE_BANK_ROLE_GENERATOR));
    assert(pwm_profile_get_bank_state(PWM_PROFILE_BANK_HW) == PWM_PROFILE_BANK_STATE_UNLOCKED);

    /* Lock HW bank as generator: fixed slice-B GPIOs, channels 0..7. */
    assert(pwm_profile_lock_bank(PWM_PROFILE_BANK_HW, PWM_PROFILE_BANK_ROLE_GENERATOR));
    assert(pwm_profile_get_bank_state(PWM_PROFILE_BANK_HW) == PWM_PROFILE_BANK_STATE_GENERATOR);
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
    /* Locking is one-shot: a second attempt on the same bank is rejected. */
    assert(!pwm_profile_lock_bank(PWM_PROFILE_BANK_HW, PWM_PROFILE_BANK_ROLE_MONITOR));

    /* Lock PIO bank as monitor: fixed companion slice-A GPIOs, channels 8..15. */
    assert(pwm_profile_lock_bank(PWM_PROFILE_BANK_PIO, PWM_PROFILE_BANK_ROLE_MONITOR));
    assert(pwm_profile_get_bank_state(PWM_PROFILE_BANK_PIO) == PWM_PROFILE_BANK_STATE_MONITOR);
    {
        const pwm_profile_channel_t *profile = pwm_profile_get_channel(8u);
        assert(profile->gpio == 0u);
        assert(profile->backend == PWM_PROFILE_BACKEND_PIO_MONITOR);
        assert(profile->direction == PWM_PROFILE_DIRECTION_INPUT);
    }

    /* Lock SW bank as generator: any remaining GPIO, channels 16..23. */
    assert(pwm_profile_lock_bank(PWM_PROFILE_BANK_SW, PWM_PROFILE_BANK_ROLE_GENERATOR));
    assert(pwm_profile_get_bank_state(PWM_PROFILE_BANK_SW) == PWM_PROFILE_BANK_STATE_GENERATOR);
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
