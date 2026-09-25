/**
 * @file shell.c
 * @brief microrl-backed interactive command shell for PicoPWM CLI transports.
 */

#include "cli/shell.h"

#include "microrl.h"

#include <string.h>

/** @brief Maximum bytes read from the transport per poll iteration. */
#define CLI_EDITOR_RX_CHUNK_SIZE 16u

/** @brief Mutable runtime state for the singleton microrl adapter. */
typedef struct {
    shell_transport_t transport; /**< Active transport callbacks and context. */
    const shell_command_t *commands; /**< Registered static command table. */
    uint32_t command_count; /**< Number of entries in @ref commands. */
    const char *unknown_message; /**< Fallback message for unmatched commands. */
    shell_unknown_handler_t unknown_handler; /**< Optional unmatched-command callback. */
    microrl_t microrl; /**< Interactive line editor state. */
    bool initialized; /**< Indicates whether @ref shell_init completed successfully. */
} shell_state_t;

/** @brief Singleton CLI editor runtime state. */
static shell_state_t shell_state;

static void shell_microrl_print(const char *text) {
    shell_write(text);
}

/**
 * @brief Write raw bytes through the active shell transport.
 * @param data Source bytes to write.
 * @param length Number of bytes to write.
 * @return `true` when the bytes were accepted, otherwise `false`.
 */
static bool shell_write_bytes(const uint8_t *data, uint32_t length) {
    if ((shell_state.transport.write == NULL) || (data == NULL)) {
        return false;
    }

    if (length == 0u) {
        return true;
    }

    return shell_state.transport.write(shell_state.transport.context, data, length);
}

/** @copydoc shell_write */
bool shell_write(const char *text) {
    if (text == NULL) {
        return false;
    }

    return shell_write_bytes((const uint8_t *)text, (uint32_t)strlen(text));
}

/** @copydoc shell_write_line */
bool shell_write_line(const char *text) {
    if ((text != NULL) && !shell_write(text)) {
        return false;
    }

    return shell_write_bytes((const uint8_t *)"\r\n", 2u);
}

/**
 * @brief Look up one command by its first-token name.
 * @param name Null-terminated command token to search for.
 * @return Matching command entry, or `NULL` when no command matches.
 */
static const shell_command_t *shell_find_command(const char *name) {
    for (uint32_t index = 0u; index < shell_state.command_count; ++index) {
        if (strcmp(shell_state.commands[index].name, name) == 0) {
            return &shell_state.commands[index];
        }
    }

    return NULL;
}

static int shell_execute(int argc, const char *const *argv) {
    const shell_command_t *command;

    command = shell_find_command(argv[0]);
    if (command == NULL) {
        if ((shell_state.unknown_handler == NULL) || !shell_state.unknown_handler(argv[0])) {
            shell_write_line(shell_state.unknown_message);
        }
        return 0;
    }

    return command->handler(argc, argv) ? 0 : -1;
}

void shell_prompt(void) {
    if (shell_state.initialized) {
        shell_state.microrl.print(shell_state.microrl.prompt_str);
    }
}

/** @copydoc shell_init */
void shell_init(const shell_config_t *config) {
    memset(&shell_state, 0, sizeof(shell_state));

    if ((config == NULL) || (config->transport == NULL) || (config->transport->read == NULL) || (config->transport->write == NULL)) {
        return;
    }

    shell_state.transport = *config->transport;
    shell_state.commands = config->commands;
    shell_state.command_count = config->command_count;
    shell_state.unknown_message = (config->unknown_message != NULL) ? config->unknown_message : "Unknown command";
    shell_state.unknown_handler = config->unknown_handler;
    microrl_init(&shell_state.microrl, shell_microrl_print);
    microrl_set_execute_callback(&shell_state.microrl, shell_execute);
    shell_state.initialized = true;
}

/** @copydoc shell_poll */
void shell_poll(void) {
    uint8_t rx_data[CLI_EDITOR_RX_CHUNK_SIZE];

    if (!shell_state.initialized) {
        return;
    }

    while (true) {
        uint32_t rx_length = shell_state.transport.read(shell_state.transport.context, rx_data, sizeof(rx_data));

        if (rx_length == 0u) {
            return;
        }

        for (uint32_t index = 0u; index < rx_length; ++index) {
            microrl_insert_char(&shell_state.microrl, rx_data[index]);
        }
    }
}