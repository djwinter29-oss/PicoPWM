/**
 * @file device_cli.c
 * @brief PicoPWM command table layered on top of the generic CLI shell.
 */

#include "cli/device_cli.h"

#include "control/control_iface.h"
#include "pwmdriver/pwm_driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Handle the `help` command. */
static bool device_cli_help(int argc, const char *const *argv);
/** @brief Handle the `info` command. */
static bool device_cli_info(int argc, const char *const *argv);
/** @brief Handle the `version` command. */
static bool device_cli_version(int argc, const char *const *argv);
/** @brief Handle the `get` command. */
static bool device_cli_get(int argc, const char *const *argv);
/** @brief Handle the `set` command. */
static bool device_cli_set(int argc, const char *const *argv);
/** @brief Handle the `h` command. */
static bool device_cli_hw(int argc, const char *const *argv);
/** @brief Handle the `p` command. */
static bool device_cli_pio(int argc, const char *const *argv);
/** @brief Handle the `s` command. */
static bool device_cli_sw(int argc, const char *const *argv);
/** @brief Handle the `d` command. */
static bool device_cli_sw_duty(int argc, const char *const *argv);
/** @brief Handle the `stop` command. */
static bool device_cli_stop(int argc, const char *const *argv);
/** @brief Handle the `status` command. */
static bool device_cli_status(int argc, const char *const *argv);
/** @brief Handle unknown command names by printing help. */
static bool device_cli_unknown(const char *command_name);

/** @brief Static command table registered with the CLI shell. */
static const cli_shell_command_t device_cli_commands[] = {
    {"help", "help                    Show this help", device_cli_help},
    {"info", "info                    Show device type", device_cli_info},
    {"version", "version              Show firmware version", device_cli_version},
    {"get", "get <ch>                Read channel properties (ch=0..23)", device_cli_get},
    {"set", "set <ch> <f|d> <value>  Set frequency or duty", device_cli_set},
    {"h", "h <ch> <freq> <duty%>   Set HW PWM channel (ch=0..7)", device_cli_hw},
    {"p", "p <ch> <freq> <duty%>   Set PIO PWM channel (ch=0..7)", device_cli_pio},
    {"s", "s <ch> <freq> <duty%>   Set SW PWM channel (ch=0..7)", device_cli_sw},
    {"d", "d <ch> <duty%>          Set SW PWM duty only (ch=0..7)", device_cli_sw_duty},
    {"stop", "stop                    Stop all channels and reset defaults", device_cli_stop},
    {"status", "status                  Show all channels", device_cli_status},
    {"st", "st                      Alias for status", device_cli_status},
};

/** @brief Map write results into CLI-visible text. */
static const char *device_cli_result_text(pwm_driver_result_t result) {
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

/**
 * @brief Parse a decimal channel index.
 * @param text Null-terminated decimal input.
 * @param value_out Caller-owned destination.
 * @return `true` when parsing succeeded.
 */
static bool device_cli_parse_int(const char *text, int *value_out) {
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

/**
 * @brief Parse a decimal float value.
 * @param text Null-terminated decimal input.
 * @param value_out Caller-owned destination.
 * @return `true` when parsing succeeded.
 */
static bool device_cli_parse_float(const char *text, float *value_out) {
    char *end = NULL;
    float parsed;

    if ((text == NULL) || (value_out == NULL) || (text[0] == '\0')) {
        return false;
    }

    parsed = strtof(text, &end);
    if ((end == text) || (end == NULL) || (*end != '\0')) {
        return false;
    }

    *value_out = parsed;
    return true;
}

/** @brief Format and emit one status row. */
static bool device_cli_write_status_row(int channel, const pwm_driver_state_t *state) {
    char line[96];
    const char *type = (channel < PIO_PWM_CHANNEL_BASE) ? "HW" :
                       (channel < SW_PWM_CHANNEL_BASE) ? "PIO" : "SW";

    snprintf(line,
             sizeof(line),
             "%-2d  %-4s  %-3s  %9.3f  %6.1f  %lu",
             channel,
             type,
             state->freq_hz > 0.0f ? "ON" : "OFF",
             state->freq_hz,
             state->duty * 100.0f,
             (unsigned long)state->pulse_count);
    return cli_shell_write_line(line);
}

/** @brief Emit the fixed help listing for all registered CLI commands. */
void device_cli_print_help(void) {
    cli_shell_write_line("Unified control interface: each channel has freq, duty, pulse_count.");
    cli_shell_write_line("Logical channels: 0..7 = hardware PWM, 8..15 = PIO PWM, 16..23 = software PWM.");
    cli_shell_write_line(NULL);
    cli_shell_write_line("Commands:");
    for (uint32_t index = 0u; index < (sizeof(device_cli_commands) / sizeof(device_cli_commands[0])); ++index) {
        cli_shell_write_line(device_cli_commands[index].help);
    }
    cli_shell_write_line(NULL);
    cli_shell_write_line("Notes:");
    cli_shell_write_line("pulse_count is read-only and accumulates from power-on.");
}

static bool device_cli_help(int argc, const char *const *argv) {
    (void)argc;
    (void)argv;

    device_cli_print_help();
    return true;
}

static bool device_cli_info(int argc, const char *const *argv) {
    (void)argv;

    if (argc != 1) {
        return cli_shell_write_line("ERR usage: info");
    }

    return cli_shell_write_line(control_iface_device_name());
}

static bool device_cli_version(int argc, const char *const *argv) {
    (void)argv;

    if (argc != 1) {
        return cli_shell_write_line("ERR usage: version");
    }

    return cli_shell_write_line(control_iface_firmware_version());
}

static bool device_cli_get(int argc, const char *const *argv) {
    pwm_driver_state_t state = {0.0f, 0.5f, 0u};
    char line[96];
    int ch;

    if ((argc != 2) || !device_cli_parse_int(argv[1], &ch)) {
        return cli_shell_write_line("ERR usage: get <ch>");
    }

    if ((ch < 0) || (ch >= PWM_DRIVER_CHANNEL_COUNT)) {
        snprintf(line, sizeof(line), "ERR channel %d invalid (0..23)", ch);
        return cli_shell_write_line(line);
    }

    control_iface_get_channel((uint)ch, &state);
    snprintf(line,
             sizeof(line),
             "CH%d: freq=%.3f Hz, duty=%.1f%%, pulses=%lu, enabled=%s",
             ch,
             state.freq_hz,
             state.duty * 100.0f,
             (unsigned long)state.pulse_count,
             state.freq_hz > 0.0f ? "yes" : "no");
    return cli_shell_write_line(line);
}

static bool device_cli_set(int argc, const char *const *argv) {
    char line[64];
    int ch;
    float value;

    if ((argc != 4) || !device_cli_parse_int(argv[1], &ch) || !device_cli_parse_float(argv[3], &value)) {
        return cli_shell_write_line("ERR usage: set <ch> <f|d> <value>");
    }

    if ((ch < 0) || (ch >= PWM_DRIVER_CHANNEL_COUNT)) {
        snprintf(line, sizeof(line), "ERR channel %d invalid (0..23)", ch);
        return cli_shell_write_line(line);
    }

    if ((strcmp(argv[2], "f") == 0) || (strcmp(argv[2], "freq") == 0)) {
        pwm_driver_result_t result = control_iface_set_channel_freq((uint)ch, value);
        if (result == PWM_DRIVER_RESULT_OK) {
            snprintf(line, sizeof(line), "OK CH%d freq=%.3f Hz", ch, value);
        } else {
            snprintf(line, sizeof(line), "ERR CH%d freq %s", ch, device_cli_result_text(result));
        }
        return cli_shell_write_line(line);
    }

    if ((strcmp(argv[2], "d") == 0) || (strcmp(argv[2], "duty") == 0)) {
        pwm_driver_result_t result = control_iface_set_channel_duty((uint)ch, value / 100.0f);
        if (result == PWM_DRIVER_RESULT_OK) {
            snprintf(line, sizeof(line), "OK CH%d duty=%.1f%%", ch, value);
        } else {
            snprintf(line, sizeof(line), "ERR CH%d duty %s", ch, device_cli_result_text(result));
        }
        return cli_shell_write_line(line);
    }

    snprintf(line, sizeof(line), "ERR unknown property '%s' (f/d only)", argv[2]);
    return cli_shell_write_line(line);
}

/** @brief Shared implementation for backend-specific set commands. */
static bool device_cli_set_backend(const char *tag, int base, int count, int argc, const char *const *argv) {
    char line[64];
    int ch;
    float freq;
    float duty;
    pwm_driver_result_t result;

    if ((argc != 4) || !device_cli_parse_int(argv[1], &ch) || !device_cli_parse_float(argv[2], &freq) || !device_cli_parse_float(argv[3], &duty)) {
        snprintf(line, sizeof(line), "ERR usage: %s <ch> <freq> <duty%%>", tag);
        return cli_shell_write_line(line);
    }

    if ((ch < 0) || (ch >= count)) {
        snprintf(line, sizeof(line), "ERR %s%d invalid (ch=0..7, duty=0..100)", tag, ch);
        return cli_shell_write_line(line);
    }

    result = control_iface_set_channel((uint)(ch + base), freq, duty / 100.0f);
    if (result == PWM_DRIVER_RESULT_OK) {
        snprintf(line, sizeof(line), "OK %s%d: %.3f Hz, %.1f%%", tag, ch, freq, duty);
    } else {
        snprintf(line, sizeof(line), "ERR %s%d %s", tag, ch, device_cli_result_text(result));
    }

    return cli_shell_write_line(line);
}

static bool device_cli_hw(int argc, const char *const *argv) {
    return device_cli_set_backend("HW", HW_PWM_CHANNEL_BASE, HW_PWM_COUNT, argc, argv);
}

static bool device_cli_pio(int argc, const char *const *argv) {
    return device_cli_set_backend("PIO", PIO_PWM_CHANNEL_BASE, PIO_PWM_DRIVER_COUNT, argc, argv);
}

static bool device_cli_sw(int argc, const char *const *argv) {
    return device_cli_set_backend("SW", SW_PWM_CHANNEL_BASE, SW_PWM_COUNT, argc, argv);
}

static bool device_cli_sw_duty(int argc, const char *const *argv) {
    char line[64];
    int ch;
    float duty;
    pwm_driver_result_t result;

    if ((argc != 3) || !device_cli_parse_int(argv[1], &ch) || !device_cli_parse_float(argv[2], &duty)) {
        return cli_shell_write_line("ERR usage: d <ch> <duty%>");
    }

    if ((ch < 0) || (ch >= SW_PWM_COUNT)) {
        snprintf(line, sizeof(line), "ERR SW%d invalid (ch=0..7)", ch);
        return cli_shell_write_line(line);
    }

    result = control_iface_set_channel_duty((uint)(ch + SW_PWM_CHANNEL_BASE), duty / 100.0f);
    if (result == PWM_DRIVER_RESULT_OK) {
        snprintf(line, sizeof(line), "OK SW%d duty: %.1f%%", ch, duty);
    } else {
        snprintf(line, sizeof(line), "ERR SW%d duty %s", ch, device_cli_result_text(result));
    }

    return cli_shell_write_line(line);
}

static bool device_cli_stop(int argc, const char *const *argv) {
    char line[64];
    pwm_driver_result_t result;

    (void)argv;
    if (argc != 1) {
        return cli_shell_write_line("ERR usage: stop");
    }

    result = control_iface_stop_all();
    if (result == PWM_DRIVER_RESULT_OK) {
        return cli_shell_write_line("OK all channels stopped and reset (freq=0, duty=50%)");
    }

    snprintf(line, sizeof(line), "ERR stop %s", device_cli_result_text(result));
    return cli_shell_write_line(line);
}

static bool device_cli_status(int argc, const char *const *argv) {
    pwm_driver_state_t state;

    (void)argv;
    if (argc != 1) {
        return cli_shell_write_line("ERR usage: status");
    }

    cli_shell_write_line("=== All PWM channels (logical 0..23) ===");
    cli_shell_write_line("Ch  Type  State  Freq(Hz)   Duty(%)   Pulses");
    for (int channel = 0; channel < PWM_DRIVER_CHANNEL_COUNT; ++channel) {
        state = (pwm_driver_state_t){0.0f, 0.5f, 0u};
        control_iface_get_channel((uint)channel, &state);
        device_cli_write_status_row(channel, &state);
    }
    return cli_shell_write_line(NULL);
}

static bool device_cli_unknown(const char *command_name) {
    char line[64];

    snprintf(line, sizeof(line), "ERR unknown command: '%s'. Type 'help'.", command_name);
    cli_shell_write_line(line);
    return true;
}

void device_cli_init(const cli_shell_transport_t *transport) {
    cli_shell_config_t config = {
        .transport = transport,
        .commands = device_cli_commands,
        .command_count = sizeof(device_cli_commands) / sizeof(device_cli_commands[0]),
        .unknown_message = "ERR unknown command",
        .unknown_handler = device_cli_unknown,
    };

    cli_shell_init(&config);
}

void device_cli_poll(void) {
    cli_shell_poll();
}