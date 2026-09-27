#include "cli/channel_commands.h"

#include "pwmdriver/pwm_driver_config.h"
#include "device_api/device_api.h"

#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static char output[256];
static uint32_t set_calls;
static bool get_available;
static pwm_driver_state_t get_state;
static pwm_driver_result_t set_result = PWM_DRIVER_RESULT_OK;
static uint8_t channel_count = 24u;

bool shell_write_line(shell_t *shell, const char *text) {
    (void)shell;
    if (text != NULL) {
        strncpy(output, text, sizeof(output) - 1u);
        output[sizeof(output) - 1u] = '\0';
    }
    return true;
}

const pwm_driver_config_channel_t *pwm_driver_config_get_channel(uint channel) {
    static const pwm_driver_config_channel_t profile = {
        .backend = PWM_DRIVER_CONFIG_BACKEND_HW_GENERATOR,
        .direction = PWM_DRIVER_CONFIG_DIRECTION_OUTPUT,
        .gpio = 1u,
        .backend_channel = 0u,
        .capabilities = PWM_DRIVER_CONFIG_CAP_READ | PWM_DRIVER_CONFIG_CAP_SET,
    };

    return (channel < PWM_DRIVER_CONFIG_CHANNEL_COUNT) ? &profile : NULL;
}

const char *pwm_driver_config_backend_name(pwm_driver_config_backend_t backend) {
    (void)backend;
    return "HW";
}

bool device_api_get_channel(uint channel, pwm_driver_state_t *state) {
    if ((channel >= PWM_DRIVER_CONFIG_CHANNEL_COUNT) || (state == NULL) || !get_available) {
        return false;
    }

    *state = get_state;
    return true;
}

uint8_t device_api_channel_count(void) {
    return channel_count;
}

pwm_driver_result_t device_api_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    (void)channel;
    (void)freq_hz;
    (void)duty;
    set_calls++;
    return set_result;
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
    get_available = false;
    assert(!channel_commands_status(NULL, 1, argv));
    assert(strstr(output, "ERR channel 0 unavailable") != NULL);
    assert(channel_commands_status(NULL, 2, argv));
    assert(strstr(output, "usage") != NULL);
}

static void test_get_and_set_results(void) {
    const char *get_argv[] = {"get", "0"};
    const char *bad_channel[] = {"get", "24"};
    const char *negative[] = {"get", "-1"};
    const char *junk[] = {"set", "0x", "10"};
    const char *set_argv[] = {"set", "1", "1000", "25"};
    const char *bad_duty[] = {"set", "1", "1000", "256"};
    const char *set_bad_channel[] = {"set", "24", "10"};
    const char *empty_channel[] = {"set", "", "10"};
    const char *negative_freq[] = {"set", "0", "-1"};
    const char *empty_duty[] = {"set", "0", "10", ""};
    const pwm_driver_result_t results[] = {
        PWM_DRIVER_RESULT_BUSY,    PWM_DRIVER_RESULT_INVALID,      PWM_DRIVER_RESULT_UNAVAILABLE,
        PWM_DRIVER_RESULT_TIMEOUT, PWM_DRIVER_RESULT_APPLY_FAILED,
    };

    get_available = true;
    get_state = (pwm_driver_state_t){1000u, 40u, 12u};
    output[0] = '\0';
    assert(channel_commands_get(NULL, 2, get_argv));
    assert(strstr(output, "pulses=12") != NULL);
    assert(strstr(output, "enabled=yes") != NULL);

    get_state.freq_hz = 0u;
    assert(channel_commands_get(NULL, 2, get_argv));
    assert(strstr(output, "enabled=no") != NULL);

    assert(channel_commands_get(NULL, 1, get_argv));
    assert(strstr(output, "usage") != NULL);
    assert(channel_commands_get(NULL, 2, bad_channel));
    assert(strstr(output, "invalid") != NULL);
    assert(channel_commands_get(NULL, 2, negative));
    assert(strstr(output, "invalid") != NULL);

    set_calls = 0u;
    set_result = PWM_DRIVER_RESULT_OK;
    assert(channel_commands_set(NULL, 4, set_argv));
    assert(set_calls == 1u);
    assert(strstr(output, "OK CH1") != NULL);
    assert(strstr(output, "duty=25%") != NULL);

    assert(channel_commands_set(NULL, 4, bad_duty));
    assert(strstr(output, "usage") != NULL);
    assert(channel_commands_set(NULL, 3, junk));
    assert(strstr(output, "usage") != NULL);
    assert(channel_commands_set(NULL, 3, set_bad_channel));
    assert(strstr(output, "invalid") != NULL);
    assert(channel_commands_set(NULL, 3, empty_channel));
    assert(channel_commands_set(NULL, 3, negative_freq));
    assert(channel_commands_set(NULL, 4, empty_duty));
    assert(strstr(output, "usage") != NULL);

    for (size_t index = 0u; index < sizeof(results) / sizeof(results[0]); ++index) {
        const char *argv[] = {"set", "0", "10"};

        set_result = results[index];
        assert(channel_commands_set(NULL, 3, argv));
        assert(strstr(output, "ERR CH0 set ") != NULL);
    }
}

static void test_status_prints_rows(void) {
    const char *argv[] = {"status"};

    get_available = true;
    get_state = (pwm_driver_state_t){10u, 50u, 3u};
    assert(channel_commands_status(NULL, 1, argv));
    assert(strstr(output, "ON") != NULL);

    get_state.freq_hz = 0u;
    assert(channel_commands_status(NULL, 1, argv));
    assert(strstr(output, "OFF") != NULL);

    channel_count = 0u;
    assert(channel_commands_get(NULL, 2, (const char *[]){"get", "0"}));
    assert(strstr(output, "0..0") != NULL);
    assert(channel_commands_status(NULL, 1, argv));
    channel_count = 24u;
}

int main(void) {
    test_frequency_overflow();
    test_read_failure_is_reported();
    test_status_failure_is_reported();
    test_get_and_set_results();
    test_status_prints_rows();
    return 0;
}
