/**
 * @file shell.c
 * @brief embedded-cli-backed interactive command shell for PicoPWM CLI transports.
 */

#include "app/cli/shell.h"

#include <string.h>

/** @brief Prompt printed by embedded-cli at the start of each input line. */
#define SHELL_PROMPT "pico> "

/** @brief Maximum bytes read from the transport per poll iteration. */
#define CLI_EDITOR_RX_CHUNK_SIZE 16u

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

    return shell->transport.write(shell->transport.context, data, length);
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
    if ((text != NULL) && !shell_write(shell, text)) {
        return false;
    }

    return shell_write_bytes(shell, (const uint8_t *)"\r\n", 2u);
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

    if (command == NULL) {
        if ((shell->unknown_handler == NULL) || !shell->unknown_handler(shell->command_context, cli_command->name)) {
            shell_write_line(shell, shell->unknown_message);
        }
        return;
    }

    argv[argc++] = cli_command->name;
    if (cli_command->args != NULL) {
        uint16_t count;

        embeddedCliTokenizeArgs(cli_command->args);
        count = embeddedCliGetTokenCount(cli_command->args);
        for (uint16_t pos = 1u; (pos <= count) && (argc < (int)SHELL_MAX_ARGS); ++pos) {
            argv[argc++] = embeddedCliGetToken(cli_command->args, pos);
        }
    }

    command->handler(shell->command_context, argc, argv);
}

/** @brief Run embedded-cli processing unless a handler is already running on this shell. */
static void shell_process(shell_t *shell) {
    if (shell->busy) {
        return;
    }

    shell->busy = true;
    shell->prompted = true;
    embeddedCliProcess(shell->cli);
    shell->busy = false;
}

/** @copydoc shell_init */
void shell_init(shell_t *shell, const shell_config_t *config) {
    EmbeddedCliConfig cli_config;

    if (shell == NULL) {
        return;
    }

    memset(shell, 0, sizeof(*shell));

    if ((config == NULL) || (config->transport == NULL) || (config->transport->read == NULL) ||
        (config->transport->write == NULL) || ((config->command_count > 0u) && (config->commands == NULL))) {
        return;
    }

    for (uint32_t index = 0u; index < config->command_count; ++index) {
        if ((config->commands[index].name == NULL) || (config->commands[index].handler == NULL)) {
            return;
        }
    }

    shell->transport = *config->transport;
    shell->commands = config->commands;
    shell->command_count = config->command_count;
    shell->unknown_message = (config->unknown_message != NULL) ? config->unknown_message : "Unknown command";
    shell->unknown_handler = config->unknown_handler;
    shell->command_context = config->command_context;
    if (config->command_count > SHELL_MAX_COMMANDS) {
        return;
    }

    cli_config = *embeddedCliDefaultConfig();
    cli_config.invitation = SHELL_PROMPT;
    cli_config.cmdBufferSize = 102u;
    cli_config.maxBindingCount = SHELL_MAX_COMMANDS;
    cli_config.cliBuffer = shell->cli_buffer;
    cli_config.cliBufferSize = sizeof(shell->cli_buffer);
    shell->cli = embeddedCliNew(&cli_config);
    if (shell->cli == NULL) {
        return;
    }

    shell->cli->appContext = shell;
    shell->cli->writeChar = shell_write_char;
    shell->cli->onCommand = shell_on_command;
    for (uint32_t index = 0u; index < config->command_count; ++index) {
        CliCommandBinding binding = {.name = config->commands[index].name, .help = config->commands[index].help};

        if (!embeddedCliAddBinding(shell->cli, binding)) {
            return;
        }
    }
    shell->initialized = true;
}

/** @copydoc shell_poll */
void shell_poll(shell_t *shell) {
    uint8_t rx_data[CLI_EDITOR_RX_CHUNK_SIZE];

    if ((shell == NULL) || !shell->initialized || shell->busy) {
        return;
    }

    while (true) {
        uint32_t rx_length = shell->transport.read(shell->transport.context, rx_data, sizeof(rx_data));

        if (rx_length == 0u) {
            return;
        }

        for (uint32_t index = 0u; index < rx_length; ++index) {
            embeddedCliReceiveChar(shell->cli, (char)rx_data[index]);
        }
        shell_process(shell);
    }
}

/** @copydoc shell_prompt */
void shell_prompt(shell_t *shell) {
    if ((shell == NULL) || !shell->initialized || shell->busy) {
        return;
    }

    if (shell->prompted) {
        shell_write(shell, SHELL_PROMPT);
    } else {
        shell_process(shell);
    }
}
