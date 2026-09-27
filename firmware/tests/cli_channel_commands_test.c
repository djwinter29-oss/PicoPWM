#include "cli/channel_commands.h"

#include "pwmdriver/channel_config/channel_config.h"
#include "device_api/device_api.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static char output[256];
static uint32_t set_calls;

bool shell_write_line(shell_t *shell, const char *text) {
    (void)shell;
    if (text != NULL) {
        strncpy(output, text, sizeof(output) - 1u);
        output[sizeof(output) - 1u] = '\0';
    }
    return true;
}

const pwm_profile_channel_t *pwm_profile_get_channel(uint channel) {
    static const pwm_profile_channel_t profile = {
        .backend = PWM_PROFILE_BACKEND_HW_GENERATOR,
        .direction = PWM_PROFILE_DIRECTION_OUTPUT,
        .gpio = 1u,
        .backend_channel = 0u,
        .capabilities = PWM_PROFILE_CAP_READ | PWM_PROFILE_CAP_SET,
    };

    return (channel < PWM_PROFILE_CHANNEL_COUNT) ? &profile : NULL;
}

const char *pwm_profile_backend_name(pwm_profile_backend_t backend) {
    (void)backend;
    return "HW";
}

bool device_api_get_channel(uint channel, pwm_driver_state_t *state) {
    (void)channel;
    (void)state;
    return false;
}

uint8_t device_api_channel_count(void) {
    return 24u;
}

pwm_driver_result_t device_api_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    (void)channel;
    (void)freq_hz;
    (void)duty;
    set_calls++;
    return PWM_DRIVER_RESULT_OK;
}

static void test_frequency_overflow(void) {
    const char *argv[] = {"set", "0", "4294967296"};

    output[0] = '\0';
    set_calls = 0u;
    assert(channel_commands_set(NULL, 3, argv));
    assert(set_calls == 0u);
    assert(strstr(output, "usage") != NULL);
}

static void test_read_failure_is_reported(void) {
    const char *argv[] = {"get", "0"};

    output[0] = '\0';
    assert(channel_commands_get(NULL, 2, argv));
    assert(strcmp(output, "ERR channel unavailable") == 0);
}

static void test_status_failure_is_reported(void) {
    const char *argv[] = {"status"};

    output[0] = '\0';
    assert(!channel_commands_status(NULL, 1, argv));
    assert(strstr(output, "ERR channel 0 unavailable") != NULL);
}

int main(void) {
    test_frequency_overflow();
    test_read_failure_is_reported();
    test_status_failure_is_reported();
    return 0;
}
