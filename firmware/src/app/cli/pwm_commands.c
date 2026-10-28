/**
 * @file pwm_commands.c
 * @brief PicoPWM command registry and shell integration.
 */

#include "app/cli/pwm_commands.h"

#include "app/cli/channel_commands.h"
#include "app/cli/board_commands.h"

#include <stdio.h>
#include <string.h>

static const shell_command_t pwm_commands[] = {
    {"info", "info                    Show device type and system clock", board_commands_info},
    {"version", "version              Show firmware version", board_commands_version},
    {"config", "config [set <a|b|c> <hw|pio|sw> <gen|mon>|save]  Show or set startup config", board_commands_config},
    {"get", "get <ch>                 Read channel properties", channel_commands_get},
    {"set", "set <ch> <freq> [duty%]  Set freq, optional duty defaults to 50%", channel_commands_set},
    {"led", "led <on|off>             Set board LED state", board_commands_led},
    {"reboot", "reboot                Reboot the board", board_commands_reboot},
    {"stop", "stop                    Stop all channels and reset defaults", board_commands_stop},
    {"status", "status                Show all configured channels", channel_commands_status},
};

static void pwm_commands_write_help(shell_t *shell) {
    shell_write_line(shell, "Unified control interface: each channel has freq and duty.");
    shell_write_line(shell, "Logical channels and backend assignment are firmware-configured.");
    shell_write_line(shell, NULL);
    shell_write_line(shell, "Commands:");
    shell_write_line(shell, "help [cmd]              Show this help");
    for (uint32_t index = 0u; index < (sizeof(pwm_commands) / sizeof(pwm_commands[0])); ++index) {
        shell_write_line(shell, pwm_commands[index].help);
    }
}

static bool pwm_commands_unknown(void *context, const char *command_name) {
    char line[64];

    snprintf(line, sizeof(line), "ERR unknown command: '%s'. Type 'help'.", command_name);
    shell_write_line((shell_t *)context, line);
    return true;
}

void pwm_commands_init(pwm_commands_t *commands, const shell_transport_t *transport) {
    shell_config_t config = {
        .transport = transport,
        .commands = pwm_commands,
        .command_count = sizeof(pwm_commands) / sizeof(pwm_commands[0]),
        .unknown_message = "ERR unknown command",
        .unknown_handler = pwm_commands_unknown,
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
