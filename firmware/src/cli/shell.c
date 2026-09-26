/**
 * @file shell.c
 * @brief microrl-backed interactive command shell for PicoPWM CLI transports.
 */

#include "cli/shell.h"

#include "microrl.h"

#include <string.h>

/** @brief Maximum bytes read from the transport per poll iteration. */
#define CLI_EDITOR_RX_CHUNK_SIZE 16u

/** @brief Shell receiving callbacks from the microrl trampoline. */
static shell_t *shell_callback_target;

static void shell_microrl_print(const char *text) {
    if (shell_callback_target != NULL) {
        shell_write(shell_callback_target, text);
    }
}

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

static int shell_execute(int argc, const char *const *argv) {
    const shell_command_t *command;

    command = shell_find_command(shell_callback_target, argv[0]);
    if (command == NULL) {
        if ((shell_callback_target->unknown_handler == NULL) ||
            !shell_callback_target->unknown_handler(shell_callback_target->command_context, argv[0])) {
            shell_write_line(shell_callback_target, shell_callback_target->unknown_message);
        }
        return 0;
    }

    return command->handler(shell_callback_target->command_context, argc, argv) ? 0 : -1;
}

static char **shell_complete(int argc, const char *const *argv) {
    if ((shell_callback_target == NULL) || (shell_callback_target->completion_handler == NULL)) {
        return NULL;
    }

    return shell_callback_target->completion_handler(shell_callback_target->command_context, argc, argv);
}

/** @copydoc shell_init */
void shell_init(shell_t *shell, const shell_config_t *config) {
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
    shell->completion_handler = config->completion_handler;
    shell->command_context = config->command_context;
    microrl_init(&shell->microrl, shell_microrl_print);
    shell_callback_target = NULL;
    microrl_set_execute_callback(&shell->microrl, shell_execute);
    if (shell->completion_handler != NULL) {
        microrl_set_complete_callback(&shell->microrl, shell_complete);
    }
    shell->initialized = true;
}

/** @copydoc shell_poll */
void shell_poll(shell_t *shell) {
    uint8_t rx_data[CLI_EDITOR_RX_CHUNK_SIZE];

    if ((shell == NULL) || !shell->initialized) {
        return;
    }

    if (shell_callback_target != NULL) {
        return;
    }

    shell_callback_target = shell;
    while (true) {
        uint32_t rx_length = shell->transport.read(shell->transport.context, rx_data, sizeof(rx_data));

        if (rx_length == 0u) {
            shell_callback_target = NULL;
            return;
        }

        for (uint32_t index = 0u; index < rx_length; ++index) {
            uint8_t byte = rx_data[index];

            if (byte == '\r' || byte == '\n') {
                microrl_insert_char(&shell->microrl, KEY_CR);
            } else {
                microrl_insert_char(&shell->microrl, byte);
            }
        }
    }
}

void shell_prompt(shell_t *shell) {
    if ((shell != NULL) && shell->initialized) {
        if (shell_callback_target != NULL) {
            return;
        }
        shell_callback_target = shell;
        shell->microrl.print(shell->microrl.prompt_str);
        shell_callback_target = NULL;
    }
}