/**
 * @file pwm_commands.c
 * @brief PicoPWM command groups and shell integration.
 */

#include "app/cli/pwm_commands.h"

#include "app/cli/board_commands.h"
#include "app/cli/channel_commands.h"
#include "app/cli/cli_usage.h"

#include <string.h>

static bool pwm_commands_pwm(void *context, int argc, const char *const *argv);
static bool pwm_commands_board(void *context, int argc, const char *const *argv);
static bool pwm_commands_config(void *context, int argc, const char *const *argv);
static bool pwm_commands_question(void *context, int argc, const char *const *argv);

/** @brief Top-level commands exposed by the CDC shell. */
static const shell_command_t pwm_commands[] = {
    {"pwm", CLI_SYNTAX_PWM_GET " | " CLI_SYNTAX_PWM_SET " | " CLI_SYNTAX_PWM_STOP, pwm_commands_pwm},
    {"board", CLI_SYNTAX_BOARD_GET " | " CLI_SYNTAX_BOARD_SET " | " CLI_SYNTAX_BOARD_REBOOT, pwm_commands_board},
    {"config", CLI_SYNTAX_CONFIG_GET " | " CLI_SYNTAX_CONFIG_SET, pwm_commands_config},
    {"?", "List command groups and available commands", pwm_commands_question},
};

/** @brief Print the commands belonging to one group. */
static bool pwm_commands_write_group_help(shell_t *shell, const char *group) {
    if (group == NULL) {
        return shell_write_line(shell, "Groups: pwm, board, config") &&
               shell_write_line(shell, "Use 'help <group>' or '<group> ?' for command syntax.");
    }

    if (strcmp(group, "pwm") == 0) {
        return shell_write_line(shell, CLI_SYNTAX_PWM_GET) && shell_write_line(shell, CLI_SYNTAX_PWM_SET) &&
               shell_write_line(shell, CLI_SYNTAX_PWM_STOP);
    }
    if (strcmp(group, "board") == 0) {
        return shell_write_line(shell, CLI_SYNTAX_BOARD_GET) && shell_write_line(shell, CLI_SYNTAX_BOARD_SET) &&
               shell_write_line(shell, CLI_SYNTAX_BOARD_REBOOT);
    }
    if (strcmp(group, "config") == 0) {
        return shell_write_line(shell, CLI_SYNTAX_CONFIG_GET) && shell_write_line(shell, CLI_SYNTAX_CONFIG_SET);
    }

    return shell_write_line(shell, "ERR unknown command group (pwm, board, config)");
}

/** @brief Dispatch PWM verbs and their parameters. */
static bool pwm_commands_pwm(void *context, int argc, const char *const *argv) {
    shell_t *shell = context;
    const char *const get_all[] = {"status"};
    const char *const stop_all[] = {"stop"};

    if ((argc == 1) || ((argc == 2) && (strcmp(argv[1], "?") == 0))) {
        return pwm_commands_write_group_help(shell, "pwm");
    }
    if (strcmp(argv[1], "get") == 0) {
        if ((argc == 2) || ((argc == 3) && (strcmp(argv[2], "?") == 0))) {
            return shell_write_line(shell, CLI_USAGE_PWM_GET);
        }
        if ((argc == 3) && (strcmp(argv[2], "all") == 0)) {
            return channel_commands_status(shell, 1, get_all);
        }
        if (argc == 3) {
            const char *channel_argv[] = {"get", argv[2]};
            return channel_commands_get(shell, 2, channel_argv);
        }
        return shell_write_line(shell, "ERR " CLI_SYNTAX_PWM_GET);
    }
    if (strcmp(argv[1], "set") == 0) {
        if ((argc == 2) || ((argc == 3) && (strcmp(argv[2], "?") == 0))) {
            return shell_write_line(shell, CLI_USAGE_PWM_SET);
        }
        if ((argc == 4) || (argc == 5)) {
            const char *set_argv[] = {"set", argv[2], argv[3], argc == 5 ? argv[4] : NULL};
            return channel_commands_set(shell, argc - 1, set_argv);
        }
        return shell_write_line(shell, "ERR " CLI_SYNTAX_PWM_SET);
    }
    if (strcmp(argv[1], "stop") == 0) {
        if ((argc == 2) || ((argc == 3) && (strcmp(argv[2], "?") == 0))) {
            return shell_write_line(shell, CLI_USAGE_PWM_STOP);
        }
        if ((argc == 3) && (strcmp(argv[2], "all") == 0)) {
            return board_commands_stop(shell, 1, stop_all);
        }
        return shell_write_line(shell, "ERR " CLI_SYNTAX_PWM_STOP);
    }

    return shell_write_line(shell, "ERR unknown PWM command; use 'pwm ?'");
}

/** @brief Dispatch board verbs and their parameters. */
static bool pwm_commands_board(void *context, int argc, const char *const *argv) {
    shell_t *shell = context;

    if ((argc == 1) || ((argc == 2) && (strcmp(argv[1], "?") == 0))) {
        return pwm_commands_write_group_help(shell, "board");
    }
    if (strcmp(argv[1], "get") == 0) {
        if ((argc == 2) || ((argc == 3) && (strcmp(argv[2], "?") == 0))) {
            return shell_write_line(shell, CLI_USAGE_BOARD_GET);
        }
        if (argc == 3) {
            return board_commands_get(shell, 3, argv);
        }
        return shell_write_line(shell, "ERR " CLI_SYNTAX_BOARD_GET);
    }
    if (strcmp(argv[1], "set") == 0) {
        if ((argc == 2) || ((argc == 3) && (strcmp(argv[2], "?") == 0))) {
            return shell_write_line(shell, CLI_USAGE_BOARD_SET);
        }
        if (argc == 4) {
            return board_commands_set(shell, argc, argv);
        }
        return shell_write_line(shell, "ERR " CLI_SYNTAX_BOARD_SET);
    }
    if ((strcmp(argv[1], "reboot") == 0) && ((argc == 2) || ((argc == 3) && (strcmp(argv[2], "?") == 0)))) {
        if (argc == 3) {
            return shell_write_line(shell, CLI_USAGE_BOARD_REBOOT);
        }
        return board_commands_reboot(shell, 1, (const char *const[]){"reboot"});
    }
    return shell_write_line(shell, "ERR unknown board command; use 'board ?'");
}

/** @brief Dispatch configuration verbs and their parameters. */
static bool pwm_commands_config(void *context, int argc, const char *const *argv) {
    shell_t *shell = context;

    if ((argc == 1) || ((argc == 2) && (strcmp(argv[1], "?") == 0))) {
        return pwm_commands_write_group_help(shell, "config");
    }
    if ((strcmp(argv[1], "get") == 0) || (strcmp(argv[1], "set") == 0)) {
        return board_commands_config(shell, argc, argv);
    }

    return shell_write_line(shell, "ERR unknown config command; use 'config ?'");
}

/** @brief Dispatch the top-level question-mark help alias. */
static bool pwm_commands_question(void *context, int argc, const char *const *argv) {
    shell_t *shell = context;

    (void)argv;
    if (argc != 1) {
        return shell_write_line(shell, "Usage: ?");
    }
    return pwm_commands_write_group_help(shell, NULL);
}

/** @brief Handle commands that do not match a registered group. */
static bool pwm_commands_unknown(void *context, const char *command_name) {
    shell_t *shell = context;

    (void)command_name;
    return shell_write_line(shell, "ERR unknown command; use 'help' or '?'");
}

/** @copydoc pwm_commands_init */
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

/** @copydoc pwm_commands_on_connected */
void pwm_commands_on_connected(pwm_commands_t *commands) {
    if (commands == NULL) {
        return;
    }

    pwm_commands_write_group_help(&commands->shell, NULL);
    shell_prompt(&commands->shell);
}

/** @copydoc pwm_commands_poll */
void pwm_commands_poll(pwm_commands_t *commands) {
    if (commands != NULL) {
        shell_poll(&commands->shell);
    }
}
