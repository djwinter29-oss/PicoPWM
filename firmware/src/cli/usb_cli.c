/**
 * @file usb_cli.c
 * @brief microrl-backed interactive command shell for PicoPWM CLI transports.
 */

#include "cli/usb_cli.h"

#include "microrl.h"

#include <string.h>

/** @brief Maximum bytes read from the transport per poll iteration. */
#define CLI_EDITOR_RX_CHUNK_SIZE 16u

/** @brief Mutable runtime state for the singleton microrl adapter. */
typedef struct {
    usb_cli_transport_t transport; /**< Active transport callbacks and context. */
    const usb_cli_command_t *commands; /**< Registered static command table. */
    uint32_t command_count; /**< Number of entries in @ref commands. */
    const char *unknown_message; /**< Fallback message for unmatched commands. */
    usb_cli_unknown_handler_t unknown_handler; /**< Optional unmatched-command callback. */
    microrl_t microrl; /**< Interactive line editor state. */
    bool initialized; /**< Indicates whether @ref usb_cli_init completed successfully. */
} usb_cli_state_t;

/** @brief Singleton CLI editor runtime state. */
static usb_cli_state_t usb_cli_state;

static void usb_cli_microrl_print(const char *text) {
    usb_cli_write(text);
}

/**
 * @brief Write raw bytes through the active shell transport.
 * @param data Source bytes to write.
 * @param length Number of bytes to write.
 * @return `true` when the bytes were accepted, otherwise `false`.
 */
static bool usb_cli_write_bytes(const uint8_t *data, uint32_t length) {
    if ((usb_cli_state.transport.write == NULL) || (data == NULL)) {
        return false;
    }

    if (length == 0u) {
        return true;
    }

    return usb_cli_state.transport.write(usb_cli_state.transport.context, data, length);
}

/** @copydoc usb_cli_write */
bool usb_cli_write(const char *text) {
    if (text == NULL) {
        return false;
    }

    return usb_cli_write_bytes((const uint8_t *)text, (uint32_t)strlen(text));
}

/** @copydoc usb_cli_write_line */
bool usb_cli_write_line(const char *text) {
    if ((text != NULL) && !usb_cli_write(text)) {
        return false;
    }

    return usb_cli_write_bytes((const uint8_t *)"\r\n", 2u);
}

/**
 * @brief Look up one command by its first-token name.
 * @param name Null-terminated command token to search for.
 * @return Matching command entry, or `NULL` when no command matches.
 */
static const usb_cli_command_t *usb_cli_find_command(const char *name) {
    for (uint32_t index = 0u; index < usb_cli_state.command_count; ++index) {
        if (strcmp(usb_cli_state.commands[index].name, name) == 0) {
            return &usb_cli_state.commands[index];
        }
    }

    return NULL;
}

static int usb_cli_execute(int argc, const char *const *argv) {
    const usb_cli_command_t *command;

    command = usb_cli_find_command(argv[0]);
    if (command == NULL) {
        if ((usb_cli_state.unknown_handler == NULL) || !usb_cli_state.unknown_handler(argv[0])) {
            usb_cli_write_line(usb_cli_state.unknown_message);
        }
        return 0;
    }

    return command->handler(argc, argv) ? 0 : -1;
}

void usb_cli_prompt(void) {
    if (usb_cli_state.initialized) {
        usb_cli_state.microrl.print(usb_cli_state.microrl.prompt_str);
    }
}

/** @copydoc usb_cli_init */
void usb_cli_init(const usb_cli_config_t *config) {
    memset(&usb_cli_state, 0, sizeof(usb_cli_state));

    if ((config == NULL) || (config->transport == NULL) || (config->transport->read == NULL) || (config->transport->write == NULL)) {
        return;
    }

    usb_cli_state.transport = *config->transport;
    usb_cli_state.commands = config->commands;
    usb_cli_state.command_count = config->command_count;
    usb_cli_state.unknown_message = (config->unknown_message != NULL) ? config->unknown_message : "Unknown command";
    usb_cli_state.unknown_handler = config->unknown_handler;
    microrl_init(&usb_cli_state.microrl, usb_cli_microrl_print);
    microrl_set_execute_callback(&usb_cli_state.microrl, usb_cli_execute);
    usb_cli_state.initialized = true;
}

/** @copydoc usb_cli_poll */
void usb_cli_poll(void) {
    uint8_t rx_data[CLI_EDITOR_RX_CHUNK_SIZE];

    if (!usb_cli_state.initialized) {
        return;
    }

    while (true) {
        uint32_t rx_length = usb_cli_state.transport.read(usb_cli_state.transport.context, rx_data, sizeof(rx_data));

        if (rx_length == 0u) {
            return;
        }

        for (uint32_t index = 0u; index < rx_length; ++index) {
            microrl_insert_char(&usb_cli_state.microrl, rx_data[index]);
        }
    }
}