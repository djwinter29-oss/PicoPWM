/**
 * @file channel_commands.c
 * @brief PWM channel validation, operation, and response formatting for the CDC CLI.
 */

#include "app/cli/channel_commands.h"

#include "app/cli/cli_usage.h"
#include "driver/pwm/pwm_driver.h"
#include "device.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static const char *channel_commands_result_text(pwm_driver_result_t result) {
    switch (result) {
    case PWM_DRIVER_RESULT_BUSY:
        return "busy";
    case PWM_DRIVER_RESULT_INVALID:
        return "invalid";
    case PWM_DRIVER_RESULT_UNAVAILABLE:
        return "unavailable";
    case PWM_DRIVER_RESULT_TIMEOUT:
        return "timeout";
    case PWM_DRIVER_RESULT_APPLY_FAILED:
        return "apply failed";
    case PWM_DRIVER_RESULT_OK:
        return "ok";
    default:
        return "unknown";
    }
}

static bool channel_commands_parse_int(const char *text, int *value_out) {
    char *end = NULL;
    long parsed;

    if ((text == NULL) || (value_out == NULL) || (text[0] == '\0')) {
        return false;
    }

    errno = 0;
    parsed = strtol(text, &end, 10);
    if ((errno == ERANGE) || (end == text) || (end == NULL) || (*end != '\0') || (parsed < INT_MIN) ||
        (parsed > INT_MAX)) {
        return false;
    }

    *value_out = (int)parsed;
    return true;
}

static bool channel_commands_parse_u32(const char *text, uint32_t *value_out) {
    char *end = NULL;
    unsigned long parsed;

    if ((text == NULL) || (value_out == NULL) || (text[0] == '\0') || (text[0] == '-')) {
        return false;
    }

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if ((errno == ERANGE) || (end == text) || (end == NULL) || (*end != '\0') || (parsed > UINT32_MAX)) {
        return false;
    }

    *value_out = (uint32_t)parsed;
    return true;
}

static bool channel_commands_parse_u8(const char *text, uint8_t *value_out) {
    char *end = NULL;
    unsigned long parsed;

    if ((text == NULL) || (value_out == NULL) || (text[0] == '\0')) {
        return false;
    }

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if ((errno == ERANGE) || (end == text) || (end == NULL) || (*end != '\0') || (parsed > UINT8_MAX)) {
        return false;
    }

    *value_out = (uint8_t)parsed;
    return true;
}

static bool channel_commands_write_status_row(shell_t *shell, int channel, const pwm_state_t *state) {
    char line[96];
    const char *type = pwm_driver_config_channel_name((uint)channel);

    snprintf(line, sizeof(line), "%-2d  %-7s  %-3s  %9lu  %6u", channel, type, state->freq_hz > 0u ? "ON" : "OFF",
             (unsigned long)state->freq_hz, (unsigned)state->duty);
    return shell_write_line(shell, line);
}

static uint8_t channel_commands_channel_count(void) {
    return device_channel_count();
}

static void channel_commands_format_invalid_channel(char *line, size_t line_size, int channel) {
    uint8_t channel_count = channel_commands_channel_count();
    unsigned int last_channel = (channel_count > 0u) ? (unsigned int)channel_count - 1u : 0u;

    snprintf(line, line_size, "ERR channel %d invalid (0..%u)", channel, last_channel);
}

/** @copydoc channel_commands_get */
bool channel_commands_get(shell_t *shell, const char *channel_text) {
    pwm_state_t state = {0u, 50u};
    char line[96];
    int channel;

    if (!channel_commands_parse_int(channel_text, &channel)) {
        return shell_write_line(shell, "ERR usage: " CLI_SYNTAX_PWM_GET);
    }

    if ((channel < 0) || (channel >= (int)channel_commands_channel_count())) {
        channel_commands_format_invalid_channel(line, sizeof(line), channel);
        return shell_write_line(shell, line);
    }

    if (!device_get_channel((uint)channel, &state)) {
        return shell_write_line(shell, "ERR channel unavailable");
    }
    snprintf(line, sizeof(line), "CH%d: freq=%lu Hz, duty=%u%%, enabled=%s", channel, (unsigned long)state.freq_hz,
             (unsigned)state.duty, state.freq_hz > 0u ? "yes" : "no");
    return shell_write_line(shell, line);
}

/** @copydoc channel_commands_set */
bool channel_commands_set(shell_t *shell, const char *channel_text, const char *frequency_text,
                          const char *duty_text) {
    char line[64];
    int channel;
    uint32_t frequency;
    uint8_t duty = 50u;
    pwm_driver_result_t result;

    if (!channel_commands_parse_int(channel_text, &channel) ||
        !channel_commands_parse_u32(frequency_text, &frequency)) {
        return shell_write_line(shell, "ERR usage: " CLI_SYNTAX_PWM_SET);
    }

    if ((channel < 0) || (channel >= (int)channel_commands_channel_count())) {
        channel_commands_format_invalid_channel(line, sizeof(line), channel);
        return shell_write_line(shell, line);
    }

    if ((duty_text != NULL) && (!channel_commands_parse_u8(duty_text, &duty) || (duty > 100u))) {
        return shell_write_line(shell, "ERR usage: " CLI_SYNTAX_PWM_SET);
    }

    result = device_set_channel((uint)channel, frequency, duty);
    if (result == PWM_DRIVER_RESULT_OK) {
        snprintf(line, sizeof(line), "OK CH%d freq=%lu Hz duty=%u%%", channel, (unsigned long)frequency,
                 (unsigned)duty);
    } else {
        snprintf(line, sizeof(line), "ERR CH%d set %s", channel, channel_commands_result_text(result));
    }

    return shell_write_line(shell, line);
}

/** @copydoc channel_commands_status */
bool channel_commands_status(shell_t *shell) {
    pwm_state_t state;
    char line[64];

    uint8_t channel_count = channel_commands_channel_count();
    unsigned int last_channel = (channel_count > 0u) ? (unsigned int)channel_count - 1u : 0u;

    snprintf(line, sizeof(line), "=== All PWM channels (logical 0..%u) ===", last_channel);
    if (!shell_write_line(shell, line) || !shell_write_line(shell, "Ch  Backend  State  Freq(Hz)   Duty(%)")) {
        return false;
    }
    for (uint8_t channel = 0u; channel < channel_count; ++channel) {
        state = (pwm_state_t){0u, 50u};
        if (!device_get_channel(channel, &state)) {
            snprintf(line, sizeof(line), "ERR channel %d unavailable", channel);
            return shell_write_line(shell, line);
        }
        if (!channel_commands_write_status_row(shell, (int)channel, &state)) {
            return false;
        }
    }
    return shell_write_line(shell, NULL);
}

/** @copydoc channel_commands_stop */
bool channel_commands_stop(shell_t *shell) {
    pwm_driver_result_t result = device_restore_defaults();

    if (result == PWM_DRIVER_RESULT_OK) {
        return shell_write_line(shell, "OK generator outputs stopped; monitor channels unchanged");
    }

    return shell_write_line(shell, "ERR stop failed; some generator outputs may remain active");
}
