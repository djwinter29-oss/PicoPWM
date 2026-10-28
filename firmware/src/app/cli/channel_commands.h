#ifndef CHANNEL_COMMANDS_H
#define CHANNEL_COMMANDS_H

#include "app/cli/shell.h"

/**
 * @brief Read one channel and write its realized state.
 * @param shell Shell session used to write the response.
 * @param channel_text User-provided logical channel number.
 * @return `true` when the response was accepted by the transport.
 */
bool channel_commands_get(shell_t *shell, const char *channel_text);

/**
 * @brief Validate and apply one channel update.
 * @param shell Shell session used to write the response.
 * @param channel_text User-provided logical channel number.
 * @param frequency_text User-provided frequency in Hz.
 * @param duty_text Optional duty percentage; `NULL` selects the default.
 * @return `true` when the operation response was accepted by the transport.
 */
bool channel_commands_set(shell_t *shell, const char *channel_text, const char *frequency_text,
                          const char *duty_text);

/**
 * @brief Write the realized state of every configured channel.
 * @param shell Shell session used to write the response.
 * @return `true` when all response lines were accepted by the transport.
 */
bool channel_commands_status(shell_t *shell);

/**
 * @brief Stop every configured generator output and report the operation result.
 * @param shell Shell session used to write the response.
 * @return `true` when the response was accepted by the transport.
 */
bool channel_commands_stop(shell_t *shell);

#endif
