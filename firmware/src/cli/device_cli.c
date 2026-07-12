/**
 * @file device_cli.c
 * @brief PicoPWM command table layered on top of the generic CLI shell.
 */

#include "cli/device_cli.h"

#include "control/control_iface.h"
#include "driver/led.h"
#include "driver/system.h"
#include "pwmdriver/pwm_driver.h"

#include <stdio.h>
#include <stdint.h>
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
/** @brief Handle the `led` command. */
static bool device_cli_led(int argc, const char *const *argv);
/** @brief Handle the `reboot` command. */
static bool device_cli_reboot(int argc, const char *const *argv);
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
    {"get", "get <ch>                 Read channel properties (ch=0..23)", device_cli_get},
    {"set", "set <ch> <freq> [duty%]  Set freq, optional duty defaults to 50%", device_cli_set},
    {"led", "led <on|off>             Set board LED state", device_cli_led},
    {"reboot", "reboot                Reboot the board", device_cli_reboot},
    {"stop", "stop                    Stop all channels and reset defaults", device_cli_stop},
    {"status", "status                Show all channels", device_cli_status},
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
 * @brief Parse a decimal unsigned 32-bit value.
 * @param text Null-terminated decimal input.
 * @param value_out Caller-owned destination.
 * @return `true` when parsing succeeded.
 */
static bool device_cli_parse_u32(const char *text, uint32_t *value_out) {
    char *end = NULL;
    unsigned long parsed;

    if ((text == NULL) || (value_out == NULL) || (text[0] == '\0')) {
        return false;
    }

    parsed = strtoul(text, &end, 10);
    if ((end == text) || (end == NULL) || (*end != '\0')) {
        return false;
    }

    if (parsed > UINT32_MAX) {
        return false;
    }

    *value_out = (uint32_t)parsed;
    return true;
}

/**
 * @brief Parse a decimal duty percentage.
 * @param text Null-terminated decimal input.
 * @param value_out Caller-owned destination.
 * @return `true` when parsing succeeded.
 */
static bool device_cli_parse_u8(const char *text, uint8_t *value_out) {
    char *end = NULL;
    unsigned long parsed;

    if ((text == NULL) || (value_out == NULL) || (text[0] == '\0')) {
        return false;
    }

    parsed = strtoul(text, &end, 10);
    if ((end == text) || (end == NULL) || (*end != '\0')) {
        return false;
    }

    if (parsed > UINT8_MAX) {
        return false;
    }

    *value_out = (uint8_t)parsed;
    return true;
}

/** @brief Format and emit one status row. */
static bool device_cli_write_status_row(int channel, const pwm_driver_state_t *state) {
    char line[96];
    const char *type = (channel < PIO_PWM_CHANNEL_BASE) ? "HW" :
                       (channel < SW_PWM_CHANNEL_BASE) ? "PIO" : "SW";

    snprintf(line,
             sizeof(line),
             "%-2d  %-4s  %-3s  %9lu  %6u  %lu",
             channel,
             type,
             state->freq_hz > 0u ? "ON" : "OFF",
             (unsigned long)state->freq_hz,
             (unsigned)state->duty,
             (unsigned long)state->pulse_count);
    return cli_shell_write_line(line);
}

/** @brief Emit the fixed help listing for all registered CLI commands. */
static void device_cli_write_help(void) {
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

    device_cli_write_help();
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
    pwm_driver_state_t state = {0u, 50u, 0u};
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
             "CH%d: freq=%lu Hz, duty=%u%%, pulses=%lu, enabled=%s",
             ch,
             (unsigned long)state.freq_hz,
             (unsigned)state.duty,
             (unsigned long)state.pulse_count,
             state.freq_hz > 0u ? "yes" : "no");
    return cli_shell_write_line(line);
}

static bool device_cli_set(int argc, const char *const *argv) {
    char line[64];
    int ch;
    uint32_t freq;
    uint8_t duty = 50u;
    pwm_driver_result_t result;

    if (((argc != 3) && (argc != 4)) || !device_cli_parse_int(argv[1], &ch) || !device_cli_parse_u32(argv[2], &freq)) {
        return cli_shell_write_line("ERR usage: set <ch> <freq> [duty%]");
    }

    if ((ch < 0) || (ch >= PWM_DRIVER_CHANNEL_COUNT)) {
        snprintf(line, sizeof(line), "ERR channel %d invalid (0..23)", ch);
        return cli_shell_write_line(line);
    }

    if ((argc == 4) && !device_cli_parse_u8(argv[3], &duty)) {
        return cli_shell_write_line("ERR usage: set <ch> <freq> [duty%]");
    }

    result = control_iface_set_channel((uint)ch, freq, duty);
    if (result == PWM_DRIVER_RESULT_OK) {
        snprintf(line, sizeof(line), "OK CH%d freq=%lu Hz duty=%u%%", ch, (unsigned long)freq, (unsigned)duty);
    } else {
        snprintf(line, sizeof(line), "ERR CH%d set %s", ch, device_cli_result_text(result));
    }

    return cli_shell_write_line(line);
}

static bool device_cli_led(int argc, const char *const *argv) {
    if (argc != 2) {
        return cli_shell_write_line("ERR usage: led <on|off>");
    }

    if ((strcmp(argv[1], "on") == 0) || (strcmp(argv[1], "1") == 0)) {
        led_set(true);
        return cli_shell_write_line("OK led on");
    }

    if ((strcmp(argv[1], "off") == 0) || (strcmp(argv[1], "0") == 0)) {
        led_set(false);
        return cli_shell_write_line("OK led off");
    }

    return cli_shell_write_line("ERR usage: led <on|off>");
}

static bool device_cli_reboot(int argc, const char *const *argv) {
    (void)argv;

    if (argc != 1) {
        return cli_shell_write_line("ERR usage: reboot");
    }

    cli_shell_write_line("OK rebooting");
    system_reboot();
    return true;
}

static bool device_cli_stop(int argc, const char *const *argv) {
    char line[64];
    pwm_driver_result_t result;

    (void)argv;
    if (argc != 1) {
        return cli_shell_write_line("ERR usage: stop");
    }

    result = control_iface_restore_defaults();
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
        state = (pwm_driver_state_t){0u, 50u, 0u};
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

void device_cli_on_connected(void) {
    device_cli_write_help();
}

void device_cli_poll(void) {
    cli_shell_poll();
}