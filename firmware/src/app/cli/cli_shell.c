/**
 * @file cli_shell.c
 * @brief embedded-cli-backed interactive command shell for PicoPWM CLI transports.
 */

#include "app/cli/cli_shell.h"

#include <string.h>

/** @brief Prompt printed by embedded-cli at the start of each input line. */
#define SHELL_PROMPT "pico> "

/** @brief Maximum response line bytes, including CRLF. */
#define SHELL_WRITE_LINE_BUFFER_SIZE (SHELL_MAX_OUTPUT_LINE_LENGTH + 2u)

/**
 * @brief Write raw bytes through the active shell transport.
 * @param data Source bytes to write.
 * @param length Number of bytes to write.
 * @return `true` when the bytes were accepted, otherwise `false`.
 */
static bool shell_write_bytes(shell_t *shell, const uint8_t *data, uint32_t length) {
    if ((shell == NULL) || (shell->transport.write == NULL) || (data == NULL)) {
        return false;
    }

    if (length == 0u) {
        return true;
    }

    if (!shell->transport.write(shell->transport.context, data, length)) {
        shell->transport_write_failed = true;
        return false;
    }

    return true;
}

/** @copydoc shell_write */
bool shell_write(shell_t *shell, const char *text) {
    if (text == NULL) {
        return false;
    }

    return shell_write_bytes(shell, (const uint8_t *)text, (uint32_t)strlen(text));
}

/** @copydoc shell_write_line */
bool shell_write_line(shell_t *shell, const char *text) {
    uint8_t line[SHELL_WRITE_LINE_BUFFER_SIZE];
    size_t length = (text != NULL) ? strlen(text) : 0u;

    if (length > SHELL_MAX_OUTPUT_LINE_LENGTH) {
        return false;
    }

    if (length != 0u) {
        memcpy(line, text, length);
    }
    line[length] = '\r';
    line[length + 1u] = '\n';
    return shell_write_bytes(shell, line, (uint32_t)(length + 2u));
}

/**
 * @brief Look up one command by its first-token name.
 * @param name Null-terminated command token to search for.
 * @return Matching command entry, or `NULL` when no command matches.
 */
static const shell_command_t *shell_find_command(shell_t *shell, const char *name) {
    for (uint32_t index = 0u; index < shell->command_count; ++index) {
        if (strcmp(shell->commands[index].name, name) == 0) {
            return &shell->commands[index];
        }
    }

    return NULL;
}

static void shell_write_char(EmbeddedCli *cli, char c) {
    shell_write_bytes((shell_t *)cli->appContext, (const uint8_t *)&c, 1u);
}

/** @brief Dispatch one parsed command line to its handler or the unknown-command path. */
static void shell_on_command(EmbeddedCli *cli, CliCommand *cli_command) {
    shell_t *shell = cli->appContext;
    const char *argv[SHELL_MAX_ARGS];
    int argc = 0;
    const shell_command_t *command = shell_find_command(shell, cli_command->name);

    shell->command_dispatched = true;
    shell->transport_write_failed = false;
    if (command == NULL) {
        if ((shell->unknown_handler == NULL) || !shell->unknown_handler(shell->command_context, cli_command->name)) {
            if (!shell->transport_write_failed) {
                shell_write_line(shell, shell->unknown_message);
            }
        }
        return;
    }

    argv[argc++] = cli_command->name;
    if (cli_command->args != NULL) {
        uint16_t count;

        embeddedCliTokenizeArgs(cli_command->args);
        count = embeddedCliGetTokenCount(cli_command->args);
        if (count >= SHELL_MAX_ARGS) {
            shell_write_line(shell, "ERR too many command arguments");
            return;
        }
        for (uint16_t pos = 1u; (pos <= count) && (argc < (int)SHELL_MAX_ARGS); ++pos) {
            argv[argc++] = embeddedCliGetToken(cli_command->args, pos);
        }
    }

    if (!command->handler(shell->command_context, argc, argv) && !shell->transport_write_failed) {
        shell_write_line(shell, "ERR command handler failed");
    }
}

/** @brief Run embedded-cli processing unless a handler is already running on this shell. */
static bool shell_process(shell_t *shell) {
    if (shell->busy) {
        return false;
    }

    shell->busy = true;
    shell->prompted = true;
    shell->transport_write_failed = false;
    embeddedCliProcess(shell->cli);
    shell->busy = false;
    return !shell->transport_write_failed;
}

/** @copydoc shell_init */
bool shell_init(shell_t *shell, const shell_config_t *config) {
    EmbeddedCliConfig cli_config;

    if (shell == NULL) {
        return false;
    }

    memset(shell, 0, sizeof(*shell));

    if ((config == NULL) || (config->transport == NULL) || (config->transport->read == NULL) ||
        (config->transport->write == NULL) || ((config->command_count > 0u) && (config->commands == NULL))) {
        return false;
    }

    if (config->command_count > SHELL_MAX_COMMANDS) {
        return false;
    }

    for (uint32_t index = 0u; index < config->command_count; ++index) {
        if ((config->commands[index].name == NULL) || (config->commands[index].handler == NULL)) {
            return false;
        }
    }

    shell->transport = *config->transport;
    shell->commands = config->commands;
    shell->command_count = config->command_count;
    shell->unknown_message = (config->unknown_message != NULL) ? config->unknown_message : "Unknown command";
    shell->unknown_handler = config->unknown_handler;
    shell->command_context = config->command_context;

    cli_config = *embeddedCliDefaultConfig();
    cli_config.invitation = SHELL_PROMPT;
    cli_config.cmdBufferSize = SHELL_MAX_INPUT_LENGTH + 2u;
    cli_config.maxBindingCount = SHELL_MAX_COMMANDS;
    cli_config.cliBuffer = shell->cli_buffer;
    cli_config.cliBufferSize = sizeof(shell->cli_buffer);
    shell->cli = embeddedCliNew(&cli_config);
    if (shell->cli == NULL) {
        return false;
    }

    shell->cli->appContext = shell;
    shell->cli->writeChar = shell_write_char;
    shell->cli->onCommand = shell_on_command;
    for (uint32_t index = 0u; index < config->command_count; ++index) {
        CliCommandBinding binding = {.name = config->commands[index].name, .help = config->commands[index].help};

        if (!embeddedCliAddBinding(shell->cli, binding)) {
            return false;
        }
    }
    shell->initialized = true;
    return true;
}

/** @copydoc shell_poll */
void shell_poll(shell_t *shell) {
    uint8_t rx_data;

    if ((shell == NULL) || !shell->initialized || shell->busy) {
        return;
    }

    while (true) {
        uint32_t rx_length = shell->transport.read(shell->transport.context, &rx_data, 1u);

        if (rx_length == 0u) {
            return;
        }

        shell->command_dispatched = false;
        embeddedCliReceiveChar(shell->cli, (char)rx_data);
        (void)shell_process(shell);
        if (shell->command_dispatched) {
            return;
        }
    }
}

/** @copydoc shell_prompt */
bool shell_prompt(shell_t *shell) {
    if ((shell == NULL) || !shell->initialized || shell->busy) {
        return false;
    }

    if (shell->prompted) {
        shell->transport_write_failed = false;
        return shell_write(shell, SHELL_PROMPT);
    }

    return shell_process(shell);
}
