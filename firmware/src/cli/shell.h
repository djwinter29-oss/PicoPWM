/**
 * @file shell.h
 * @brief microrl-backed interactive command shell for PicoPWM CLI transports.
 */

#ifndef SHELL_H
#define SHELL_H

#include "microrl.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Transport read callback used by the CLI shell.
 * @param context Caller-owned transport context.
 * @param data Destination buffer for received bytes.
 * @param capacity Maximum bytes to copy into @p data.
 * @return Number of bytes copied into @p data.
 */
typedef uint32_t (*shell_transport_read_t)(void *context, uint8_t *data, uint32_t capacity);

/**
 * @brief Transport write callback used by the CLI shell.
 * @param context Caller-owned transport context.
 * @param data Source buffer containing bytes to write.
 * @param length Number of bytes to write.
 * @return `true` when the bytes were accepted, otherwise `false`.
 */
typedef bool (*shell_transport_write_t)(void *context, const uint8_t *data, uint32_t length);

/** @brief Transport binding used by the CLI editor for byte-oriented I/O. */
typedef struct {
    shell_transport_read_t read; /**< Callback used to fetch received bytes. */
    shell_transport_write_t write; /**< Callback used to write response bytes. */
    void *context; /**< Caller-owned context passed back to @ref read and @ref write. */
} shell_transport_t;

/**
 * @brief Command handler invoked for one parsed CLI command line.
 * @param context Command context supplied during shell initialization.
 * @param argc Number of parsed argument tokens.
 * @param argv Null-terminated token strings backed by the shell line buffer.
 * @return `true` when the handler completed normally, otherwise `false`.
 */
typedef bool (*shell_command_handler_t)(void *context, int argc, const char *const *argv);

/**
 * @brief Unknown-command handler invoked when no registered command matches the first token.
 * @param context Command context supplied during shell initialization.
 * @param command_name First token from the unrecognized command line.
 * @return `true` when the handler already produced its own response, otherwise `false`.
 */
typedef bool (*shell_unknown_handler_t)(void *context, const char *command_name);

/**
 * @brief Return command completions for the current shell input.
 * @param context Caller-owned command context.
 * @param argc Number of parsed completion tokens.
 * @param argv Parsed completion tokens.
 * @return Null-terminated completion strings owned by the callback.
 */
typedef char **(*shell_completion_handler_t)(void *context, int argc, const char *const *argv);

/** @brief One registered CLI command entry. */
typedef struct {
    const char *name; /**< Command token matched against the first word in a line. */
    const char *help; /**< One-line help text shown by the device CLI help command. */
    shell_command_handler_t handler; /**< Callback invoked when @ref name matches. */
} shell_command_t;

/** @brief CLI editor configuration supplied during initialization. */
typedef struct {
    const shell_transport_t *transport; /**< Transport binding used for shell I/O. */
    const shell_command_t *commands; /**< Static command table visible to the shell. */
    uint32_t command_count; /**< Number of entries in @ref commands. */
    const char *unknown_message; /**< Fallback message used when no unknown-handler responds. */
    shell_unknown_handler_t unknown_handler; /**< Optional callback for unknown command names. */
    shell_completion_handler_t completion_handler; /**< Optional Tab-completion callback. */
    void *command_context; /**< Context passed to command callbacks. */
} shell_config_t;

/** @brief One independent interactive shell session.
 *
 * Shell sessions have independent microrl buffers and parser state. Only one
 * session may be inside @ref shell_poll or @ref shell_prompt at a time because
 * microrl invokes callbacks through a library-compatible process-wide bridge.
 */
typedef struct {
    shell_transport_t transport; /**< Active byte transport. */
    const shell_command_t *commands; /**< Registered command table. */
    uint32_t command_count; /**< Number of entries in @ref commands. */
    const char *unknown_message; /**< Fallback message for unmatched commands. */
    shell_unknown_handler_t unknown_handler; /**< Optional unmatched-command callback. */
    shell_completion_handler_t completion_handler; /**< Optional Tab-completion callback. */
    void *command_context; /**< Context passed to command callbacks. */
    microrl_t microrl; /**< Interactive line editor state. */
    bool initialized; /**< Indicates whether @ref shell_init completed successfully. */
} shell_t;

/**
 * @brief Initialize the generic CLI shell.
 * @param config Caller-owned shell configuration.
 */
void shell_init(shell_t *shell, const shell_config_t *config);

/** @brief Poll the shell transport, assemble input lines, and dispatch complete commands.
 * @note Calls must not be nested across shell instances.
 */
void shell_poll(shell_t *shell);

/** @brief Emit the configured interactive prompt through the shell transport. */
void shell_prompt(shell_t *shell);

/**
 * @brief Write raw text through the shell transport.
 * @param text Null-terminated text to write.
 * @return `true` when the text was accepted, otherwise `false`.
 */
bool shell_write(shell_t *shell, const char *text);

/**
 * @brief Write one text line followed by CRLF through the shell transport.
 * @param text Null-terminated line text, or `NULL` to emit only CRLF.
 * @return `true` when the line was accepted, otherwise `false`.
 */
bool shell_write_line(shell_t *shell, const char *text);

#endif