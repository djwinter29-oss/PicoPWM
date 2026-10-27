#include "driver/pwm/pwm_driver_internal.h"
#include "platform/system.h"

#include <assert.h>
#include <stdio.h>
#include <stdatomic.h>

/* Backend stubs: generators publish like the real ones; frequency 777 is rejected; bank C is a monitor. */
bool hw_gen_init(void) {
    return true;
}
bool pio_gen_init(void) {
    return true;
}
bool sw_gen_init(void) {
    return true;
}
bool hw_mon_init(void) {
    return true;
}
bool pio_mon_init(void) {
    return true;
}
bool sw_mon_init(void) {
    return true;
}
bool hw_mon_get(uint channel, pwm_state_t *state) {
    (void)channel;
    (void)state;
    return false;
}
bool pio_mon_get(uint channel, pwm_state_t *state) {
    (void)channel;
    (void)state;
    return false;
}
void sw_mon_handle_gpio_irq(uint gpio, uint32_t events) {
    (void)gpio;
    (void)events;
}

static bool test_gen_set(uint bank, uint channel, uint32_t freq_hz, uint8_t duty) {
    pwm_state_t state = {.freq_hz = freq_hz, .duty = duty};

    if (freq_hz == 777u) {
        return false;
    }
    pwm_driver_store_applied_state(bank * PWM_DRIVER_CONFIG_BANK_SIZE + channel, &state);
    return true;
}

bool hw_gen_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    return test_gen_set(0u, channel, freq_hz, duty);
}
bool pio_gen_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    return test_gen_set(1u, channel, freq_hz, duty);
}
bool sw_gen_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    (void)channel;
    (void)freq_hz;
    (void)duty;
    return false;
}

static volatile bool sw_mon_signal = true;
/** @brief Count service passes despite the SDK stub waking before its deadline. */
static atomic_uint monitor_passes;

bool sw_mon_get(uint channel, pwm_state_t *state) {
    if (channel == 0u) {
        atomic_fetch_add(&monitor_passes, 1u);
    }
    if (channel != 0u || !sw_mon_signal) {
        return false;
    }
    state->freq_hz = 123u;
    state->duty = 45u;
    return true;
}

void system_watchdog_kick(void) {}
void system_reboot(void) {
    assert(!"core 1 stalled");
}

static void expect_state(uint channel, uint32_t freq_hz, uint8_t duty) {
    pwm_state_t state;

    assert(pwm_driver_get(channel, &state));
    assert(state.freq_hz == freq_hz && state.duty == duty);
}

int main(void) {
    const pwm_role_t roles[PWM_BANK_COUNT] = {PWM_ROLE_GENERATOR, PWM_ROLE_GENERATOR, PWM_ROLE_MONITOR};
    pwm_state_t state;
    pwm_role_t role;

    assert(!pwm_driver_get(0u, &state));
    assert(pwm_driver_set(0u, 1000u, 50u) == PWM_DRIVER_RESULT_UNAVAILABLE);

    assert(pwm_driver_init(roles));
    assert(!pwm_driver_init(roles));
    for (int i = 0; i < 5000 && !pwm_driver_is_ready(); ++i) {
        usleep(1000);
    }
    assert(pwm_driver_is_ready());

    /* Set goes through the mailbox, Core 1 applies it, and the result is readable from the cache. */
    assert(pwm_driver_set(0u, 1000u, 50u) == PWM_DRIVER_RESULT_OK);
    expect_state(0u, 1000u, 50u);
    assert(pwm_driver_set(9u, 2000u, 250u) == PWM_DRIVER_RESULT_OK);
    expect_state(9u, 2000u, 100u);

    /* Rejections: backend refusal, monitor bank, bad channel, below the hardware minimum. */
    assert(pwm_driver_set(0u, 777u, 50u) == PWM_DRIVER_RESULT_APPLY_FAILED);
    expect_state(0u, 1000u, 50u);
    assert(pwm_driver_set(16u, 1000u, 50u) == PWM_DRIVER_RESULT_UNAVAILABLE);
    assert(pwm_driver_set(99u, 1000u, 50u) == PWM_DRIVER_RESULT_INVALID);
    assert(pwm_driver_set(0u, 5u, 50u) == PWM_DRIVER_RESULT_INVALID);

    /* Core 1 publishes monitor samples without any command. */
    for (int i = 0; i < 5000; ++i) {
        assert(pwm_driver_get(16u, &state));
        if (state.freq_hz == 123u) {
            break;
        }
        usleep(1000);
    }
    expect_state(16u, 123u, 45u);

    /* A lost signal reads as 0 Hz, 0% instead of the last sample. */
    sw_mon_signal = false;
    for (int i = 0; i < 5000; ++i) {
        assert(pwm_driver_get(16u, &state));
        if (state.freq_hz == 0u) {
            break;
        }
        usleep(1000);
    }
    expect_state(16u, 0u, 0u);

    assert(pwm_driver_restore_defaults() == PWM_DRIVER_RESULT_OK);
    expect_state(0u, 0u, 0u);
    expect_state(9u, 0u, 0u);
    expect_state(16u, 0u, 0u);

    assert(pwm_driver_config_channel_role(16u, &role) && role == PWM_ROLE_MONITOR);
    assert(!pwm_driver_config_channel_role(24u, &role));
    assert(pwm_driver_config_is_monitor());
    unsigned before = atomic_load(&monitor_passes);
    absolute_time_t start = get_absolute_time();
    usleep(30000);
    uint64_t elapsed = (uint64_t)absolute_time_diff_us(start, get_absolute_time());
    unsigned passes = atomic_load(&monitor_passes) - before;
    assert(passes > 0u);
    assert(passes <= elapsed / 1000u + 2u);
    puts("pwm_driver_test ok");
    return 0;
}
