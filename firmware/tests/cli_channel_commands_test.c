/**
 * @file cli_channel_commands_test.c
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#include "app/cli/cli_channel_commands.h"

#include "driver/pwm/pwm_driver.h"
#include "control_api.h"

#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static char output[256];
static uint32_t set_calls;
static uint32_t restore_defaults_calls;
static bool get_available;
static pwm_state_t get_state;
static pwm_driver_result_t set_result = PWM_DRIVER_RESULT_OK;
static pwm_driver_result_t restore_defaults_result = PWM_DRIVER_RESULT_OK;
static uint8_t channel_count = 24u;

bool shell_write_line(shell_t *shell, const char *text) {
    (void)shell;
    if (text != NULL) {
        strncpy(output, text, sizeof(output) - 1u);
        output[sizeof(output) - 1u] = '\0';
    }
    return true;
}

const char *pwm_driver_config_channel_name(uint channel) {
    (void)channel;
    return "HW";
}

bool control_api_get_channel(uint channel, pwm_state_t *state) {
    if ((channel >= PWM_DRIVER_CONFIG_CHANNEL_COUNT) || (state == NULL) || !get_available) {
        return false;
    }

    *state = get_state;
    return true;
}

uint8_t control_api_channel_count(void) {
    return channel_count;
}

pwm_driver_result_t control_api_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    (void)channel;
    (void)freq_hz;
    (void)duty;
    set_calls++;
    return set_result;
}

pwm_driver_result_t control_api_restore_defaults(void) {
    ++restore_defaults_calls;
    return restore_defaults_result;
}

static void test_stop_outputs(void) {
    restore_defaults_calls = 0u;
    restore_defaults_result = PWM_DRIVER_RESULT_OK;
    assert(channel_commands_stop(NULL));
    assert(restore_defaults_calls == 1u);
    assert(strstr(output, "OK generator outputs stopped") != NULL);

    restore_defaults_result = PWM_DRIVER_RESULT_APPLY_FAILED;
    assert(channel_commands_stop(NULL));
    assert(strstr(output, "ERR stop failed") != NULL);
    restore_defaults_result = PWM_DRIVER_RESULT_OK;
}

static void test_frequency_overflow(void) {
    output[0] = '\0';
    set_calls = 0u;
    assert(channel_commands_set(NULL, "0", "4294967296", NULL));
    assert(set_calls == 0u);
    assert(strstr(output, "usage") != NULL);
}

static void test_read_failure_is_reported(void) {
    output[0] = '\0';
    assert(channel_commands_get(NULL, "0"));
    assert(strcmp(output, "ERR channel unavailable") == 0);
}

static void test_status_failure_is_reported(void) {
    output[0] = '\0';
    get_available = false;
    assert(channel_commands_status(NULL));
    assert(strstr(output, "ERR channel 0 unavailable") != NULL);
}

static void test_get_and_set_results(void) {
    const pwm_driver_result_t results[] = {
        PWM_DRIVER_RESULT_BUSY,    PWM_DRIVER_RESULT_INVALID,      PWM_DRIVER_RESULT_UNAVAILABLE,
        PWM_DRIVER_RESULT_TIMEOUT, PWM_DRIVER_RESULT_APPLY_FAILED,
    };

    get_available = true;
    get_state = (pwm_state_t){1000u, 40u};
    output[0] = '\0';
    assert(channel_commands_get(NULL, "0"));
    assert(strstr(output, "CH0: freq=1000 Hz, duty=40%, enabled=yes") != NULL);

    get_state.freq_hz = 0u;
    assert(channel_commands_get(NULL, "0"));
    assert(strstr(output, "enabled=no") != NULL);

    assert(channel_commands_get(NULL, "24"));
    assert(strstr(output, "invalid") != NULL);
    assert(channel_commands_get(NULL, "-1"));
    assert(strstr(output, "invalid") != NULL);

    set_calls = 0u;
    set_result = PWM_DRIVER_RESULT_OK;
    assert(channel_commands_set(NULL, "1", "1000", "25"));
    assert(set_calls == 1u);
    assert(strstr(output, "OK CH1 freq=1000 Hz duty=25%") != NULL);
    assert(channel_commands_set(NULL, "1", "1000", "100"));
    assert(set_calls == 2u);
    assert(strstr(output, "duty=100%") != NULL);

    assert(channel_commands_set(NULL, "1", "1000", "256"));
    assert(strstr(output, "usage") != NULL);
    assert(channel_commands_set(NULL, "0x", "10", NULL));
    assert(strstr(output, "usage") != NULL);
    assert(channel_commands_set(NULL, "24", "10", NULL));
    assert(strstr(output, "invalid") != NULL);
    assert(channel_commands_set(NULL, "", "10", NULL));
    assert(channel_commands_set(NULL, "0", "-1", NULL));
    assert(channel_commands_set(NULL, "0", "10", ""));
    assert(strstr(output, "usage") != NULL);
    set_calls = 0u;
    assert(channel_commands_set(NULL, "1", "1000", "101"));
    assert(set_calls == 0u);
    assert(strstr(output, "usage") != NULL);

    for (size_t index = 0u; index < sizeof(results) / sizeof(results[0]); ++index) {
        set_result = results[index];
        assert(channel_commands_set(NULL, "0", "10", NULL));
        assert(strstr(output, "ERR CH0 set ") != NULL);
    }
}

static void test_status_prints_rows(void) {
    get_available = true;
    get_state = (pwm_state_t){10u, 50u};
    assert(channel_commands_status(NULL));
    assert(strstr(output, "ON") != NULL);

    get_state.freq_hz = 0u;
    assert(channel_commands_status(NULL));
    assert(strstr(output, "OFF") != NULL);

    channel_count = 0u;
    assert(channel_commands_get(NULL, "0"));
    assert(strstr(output, "0..0") != NULL);
    assert(channel_commands_status(NULL));
    channel_count = 24u;
}

int main(void) {
    test_frequency_overflow();
    test_read_failure_is_reported();
    test_status_failure_is_reported();
    test_get_and_set_results();
    test_status_prints_rows();
    test_stop_outputs();
    return 0;
}
