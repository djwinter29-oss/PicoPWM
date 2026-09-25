#include "cli/pwm_channel_commands.h"

#include "config/pwm_profile.h"
#include "control/control_iface.h"
#include "pwmdriver/pwm_driver.h"

#include <stdio.h>
#include <stdlib.h>

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

    parsed = strtol(text, &end, 10);
    if ((end == text) || (end == NULL) || (*end != '\0')) {
        return false;
    }

    *value_out = (int)parsed;
    return true;
}

static bool pwm_channel_commands_parse_u32(const char *text, uint32_t *value_out) {
    char *end = NULL;
    unsigned long parsed;

    if ((text == NULL) || (value_out == NULL) || (text[0] == '\0')) {
        return false;
    }

    parsed = strtoul(text, &end, 10);
    if ((end == text) || (end == NULL) || (*end != '\0') || (parsed > UINT32_MAX)) {
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

    parsed = strtoul(text, &end, 10);
    if ((end == text) || (end == NULL) || (*end != '\0') || (parsed > UINT8_MAX)) {
        return false;
    }

    *value_out = (uint8_t)parsed;
    return true;
}

static bool pwm_channel_commands_write_status_row(int channel, const pwm_driver_state_t *state) {
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
    return shell_write_line(line);
}

bool pwm_channel_commands_get(int argc, const char *const *argv) {
    pwm_driver_state_t state = {0u, 50u, 0u};
    char line[96];
    int channel;

    if ((argc != 2) || !pwm_channel_commands_parse_int(argv[1], &channel)) {
        return shell_write_line("ERR usage: get <ch>");
    }

    if ((channel < 0) || (channel >= PWM_DRIVER_CHANNEL_COUNT)) {
        snprintf(line, sizeof(line), "ERR channel %d invalid (0..23)", channel);
        return shell_write_line(line);
    }

    control_iface_get_channel((uint)channel, &state);
    snprintf(line,
             sizeof(line),
             "CH%d: freq=%lu Hz, duty=%u%%, pulses=%lu, enabled=%s",
             channel,
             (unsigned long)state.freq_hz,
             (unsigned)state.duty,
             (unsigned long)state.pulse_count,
             state.freq_hz > 0u ? "yes" : "no");
    return shell_write_line(line);
}

bool pwm_channel_commands_set(int argc, const char *const *argv) {
    char line[64];
    int channel;
    uint32_t frequency;
    uint8_t duty = 50u;
    pwm_driver_result_t result;

    if (((argc != 3) && (argc != 4)) || !pwm_channel_commands_parse_int(argv[1], &channel) ||
        !pwm_channel_commands_parse_u32(argv[2], &frequency)) {
        return shell_write_line("ERR usage: set <ch> <freq> [duty%]");
    }

    if ((channel < 0) || (channel >= PWM_DRIVER_CHANNEL_COUNT)) {
        snprintf(line, sizeof(line), "ERR channel %d invalid (0..23)", channel);
        return shell_write_line(line);
    }

    if ((argc == 4) && !pwm_channel_commands_parse_u8(argv[3], &duty)) {
        return shell_write_line("ERR usage: set <ch> <freq> [duty%]");
    }

    result = control_iface_set_channel((uint)channel, frequency, duty);
    if (result == PWM_DRIVER_RESULT_OK) {
        snprintf(line, sizeof(line), "OK CH%d freq=%lu Hz duty=%u%%", channel, (unsigned long)frequency, (unsigned)duty);
    } else {
        snprintf(line, sizeof(line), "ERR CH%d set %s", channel, pwm_channel_commands_result_text(result));
    }

    return shell_write_line(line);
}

bool pwm_channel_commands_status(int argc, const char *const *argv) {
    pwm_driver_state_t state;

    (void)argv;
    if (argc != 1) {
        return shell_write_line("ERR usage: status");
    }

    shell_write_line("=== All PWM channels (logical 0..23) ===");
    shell_write_line("Ch  Backend  State  Freq(Hz)   Duty(%)   Pulses");
    for (int channel = 0; channel < PWM_DRIVER_CHANNEL_COUNT; ++channel) {
        state = (pwm_driver_state_t){0u, 50u, 0u};
        control_iface_get_channel((uint)channel, &state);
        pwm_channel_commands_write_status_row(channel, &state);
    }
    return shell_write_line(NULL);
}
