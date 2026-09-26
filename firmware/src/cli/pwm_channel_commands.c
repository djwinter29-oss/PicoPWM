#include "cli/pwm_channel_commands.h"

#include "profile/pwm_profile.h"
#include "control/control_iface.h"
#include "pwmdriver/pwm_driver.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define COMMAND_SHELL ((shell_t *)context)

static const char *pwm_channel_commands_result_text(pwm_driver_result_t result) {
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
    default:
        return "ok";
    }
}

static bool pwm_channel_commands_parse_int(const char *text, int *value_out) {
    char *end = NULL;
    long parsed;

    if ((text == NULL) || (value_out == NULL) || (text[0] == '\0')) {
        return false;
    }

    errno = 0;
    parsed = strtol(text, &end, 10);
    if ((errno == ERANGE) || (end == text) || (end == NULL) || (*end != '\0') ||
        (parsed < INT_MIN) || (parsed > INT_MAX)) {
        return false;
    }

    *value_out = (int)parsed;
    return true;
}

static bool pwm_channel_commands_parse_u32(const char *text, uint32_t *value_out) {
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

static bool pwm_channel_commands_parse_u8(const char *text, uint8_t *value_out) {
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

static bool pwm_channel_commands_write_status_row(shell_t *shell, int channel, const pwm_driver_state_t *state) {
    char line[96];
    const pwm_profile_channel_t *profile = pwm_profile_get_channel((uint)channel);
    const char *type = (profile == NULL) ? "?" : pwm_profile_backend_name(profile->backend);

    snprintf(line,
             sizeof(line),
             "%-2d  %-7s  %-3s  %9lu  %6u  %lu",
             channel,
             type,
             state->freq_hz > 0u ? "ON" : "OFF",
             (unsigned long)state->freq_hz,
             (unsigned)state->duty,
             (unsigned long)state->pulse_count);
    return shell_write_line(shell, line);
}

static uint8_t pwm_channel_commands_channel_count(void) {
    return control_iface_channel_count();
}

static void pwm_channel_commands_format_invalid_channel(char *line, size_t line_size, int channel) {
    uint8_t channel_count = pwm_channel_commands_channel_count();
    unsigned int last_channel = (channel_count > 0u) ? (unsigned int)channel_count - 1u : 0u;

    snprintf(line, line_size, "ERR channel %d invalid (0..%u)", channel, last_channel);
}

bool pwm_channel_commands_get(void *context, int argc, const char *const *argv) {
    pwm_driver_state_t state = {0u, 50u, 0u};
    char line[96];
    int channel;

    if ((argc != 2) || !pwm_channel_commands_parse_int(argv[1], &channel)) {
        return shell_write_line(COMMAND_SHELL, "ERR usage: get <ch>");
    }

    if ((channel < 0) || (channel >= (int)pwm_channel_commands_channel_count())) {
        pwm_channel_commands_format_invalid_channel(line, sizeof(line), channel);
        return shell_write_line(COMMAND_SHELL, line);
    }

    if (!control_iface_get_channel((uint)channel, &state)) {
        return shell_write_line(COMMAND_SHELL, "ERR channel unavailable");
    }
    snprintf(line,
             sizeof(line),
             "CH%d: freq=%lu Hz, duty=%u%%, pulses=%lu, enabled=%s",
             channel,
             (unsigned long)state.freq_hz,
             (unsigned)state.duty,
             (unsigned long)state.pulse_count,
             state.freq_hz > 0u ? "yes" : "no");
    return shell_write_line(COMMAND_SHELL, line);
}

bool pwm_channel_commands_set(void *context, int argc, const char *const *argv) {
    char line[64];
    int channel;
    uint32_t frequency;
    uint8_t duty = 50u;
    pwm_driver_result_t result;

    if (((argc != 3) && (argc != 4)) || !pwm_channel_commands_parse_int(argv[1], &channel) ||
        !pwm_channel_commands_parse_u32(argv[2], &frequency)) {
        return shell_write_line(COMMAND_SHELL, "ERR usage: set <ch> <freq> [duty%]");
    }

    if ((channel < 0) || (channel >= (int)pwm_channel_commands_channel_count())) {
        pwm_channel_commands_format_invalid_channel(line, sizeof(line), channel);
        return shell_write_line(COMMAND_SHELL, line);
    }

    if ((argc == 4) && !pwm_channel_commands_parse_u8(argv[3], &duty)) {
        return shell_write_line(COMMAND_SHELL, "ERR usage: set <ch> <freq> [duty%]");
    }

    result = control_iface_set_channel((uint)channel, frequency, duty);
    if (result == PWM_DRIVER_RESULT_OK) {
        snprintf(line, sizeof(line), "OK CH%d freq=%lu Hz duty=%u%%", channel, (unsigned long)frequency, (unsigned)duty);
    } else {
        snprintf(line, sizeof(line), "ERR CH%d set %s", channel, pwm_channel_commands_result_text(result));
    }

    return shell_write_line(COMMAND_SHELL, line);
}

bool pwm_channel_commands_status(void *context, int argc, const char *const *argv) {
    pwm_driver_state_t state;
    char line[64];

    (void)argv;
    if (argc != 1) {
        return shell_write_line(COMMAND_SHELL, "ERR usage: status");
    }

    uint8_t channel_count = pwm_channel_commands_channel_count();
    unsigned int last_channel = (channel_count > 0u) ? (unsigned int)channel_count - 1u : 0u;

    snprintf(line, sizeof(line), "=== All PWM channels (logical 0..%u) ===", last_channel);
    shell_write_line(COMMAND_SHELL, line);
    shell_write_line(COMMAND_SHELL, "Ch  Backend  State  Freq(Hz)   Duty(%)   Pulses");
    for (uint8_t channel = 0u; channel < channel_count; ++channel) {
        state = (pwm_driver_state_t){0u, 50u, 0u};
        if (!control_iface_get_channel(channel, &state)) {
            snprintf(line, sizeof(line), "ERR channel %d unavailable", channel);
            shell_write_line(COMMAND_SHELL, line);
            return false;
        }
        pwm_channel_commands_write_status_row(COMMAND_SHELL, (int)channel, &state);
    }
    return shell_write_line(COMMAND_SHELL, NULL);
}
