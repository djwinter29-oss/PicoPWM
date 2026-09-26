/**
 * @file pwm_commands.c
 * @brief PicoPWM command registry and shell integration.
 */

#include "cli/pwm_commands.h"

#include "cli/pwm_channel_commands.h"
#include "cli/board_commands.h"

#include <stdio.h>
#include <string.h>

static bool pwm_commands_help(void *context, int argc, const char *const *argv);

static const shell_command_t pwm_commands[] = {
    {"help", "help                    Show this help", pwm_commands_help},
    {"info", "info                    Show device type", board_commands_info},
    {"version", "version              Show firmware version", board_commands_version},
    {"get", "get <ch>                 Read channel properties", pwm_channel_commands_get},
    {"set", "set <ch> <freq> [duty%]  Set freq, optional duty defaults to 50%", pwm_channel_commands_set},
    {"led", "led <on|off>             Set board LED state", board_commands_led},
    {"reboot", "reboot                Reboot the board", board_commands_reboot},
    {"stop", "stop                    Stop all channels and reset defaults", board_commands_stop},
    {"status", "status                Show all configured channels", pwm_channel_commands_status},
};

static void pwm_commands_write_help(shell_t *shell) {
    shell_write_line(shell, "Unified control interface: each channel has freq, duty, pulse_count.");
    shell_write_line(shell, "Logical channels and backend assignment are firmware-configured.");
    shell_write_line(shell, NULL);
    shell_write_line(shell, "Commands:");
    for (uint32_t index = 0u; index < (sizeof(pwm_commands) / sizeof(pwm_commands[0])); ++index) {
        shell_write_line(shell, pwm_commands[index].help);
    }
    shell_write_line(shell, NULL);
    shell_write_line(shell, "Notes:");
    shell_write_line(shell, "pulse_count is read-only and accumulates from power-on.");
}

static bool pwm_commands_help(void *context, int argc, const char *const *argv) {
    (void)argc;
    (void)argv;

    pwm_commands_write_help((shell_t *)context);
    return true;
}

static bool pwm_commands_unknown(void *context, const char *command_name) {
    char line[64];

    snprintf(line, sizeof(line), "ERR unknown command: '%s'. Type 'help'.", command_name);
    shell_write_line((shell_t *)context, line);
    return true;
}

static char **pwm_commands_complete(void *context, int argc, const char *const *argv) {
    static char *matches[sizeof(pwm_commands) / sizeof(pwm_commands[0]) + 1u];
    const char *prefix = (argc > 0) ? argv[argc - 1] : "";
    size_t prefix_length = strlen(prefix);
    uint32_t match_count = 0u;

    (void)context;
    for (uint32_t index = 0u; index < (sizeof(pwm_commands) / sizeof(pwm_commands[0])); ++index) {
        if (strncmp(pwm_commands[index].name, prefix, prefix_length) == 0) {
            matches[match_count++] = (char *)pwm_commands[index].name;
        }
    }
    matches[match_count] = NULL;
    return matches;
}

void pwm_commands_init(pwm_commands_t *commands, const shell_transport_t *transport) {
    shell_config_t config = {
        .transport = transport,
        .commands = pwm_commands,
        .command_count = sizeof(pwm_commands) / sizeof(pwm_commands[0]),
        .unknown_message = "ERR unknown command",
        .unknown_handler = pwm_commands_unknown,
        .completion_handler = pwm_commands_complete,
        .command_context = (commands != NULL) ? &commands->shell : NULL,
    };

    shell_init((commands != NULL) ? &commands->shell : NULL, &config);
}

void pwm_commands_on_connected(pwm_commands_t *commands) {
    if (commands == NULL) {
        return;
    }

    pwm_commands_write_help(&commands->shell);
    shell_prompt(&commands->shell);
}

void pwm_commands_poll(pwm_commands_t *commands) {
    if (commands != NULL) {
        shell_poll(&commands->shell);
    }
}
