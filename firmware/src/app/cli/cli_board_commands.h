/**
 * @file cli_board_commands.h
 * @brief Board and startup-configuration command handlers for the CDC shell.
 */

#ifndef CLI_BOARD_COMMANDS_H
#define CLI_BOARD_COMMANDS_H

#include "app/cli/cli_shell.h"

/**
 * @brief Read one board property.
 * @param shell Shell session.
 * @param item Property name.
 * @return True when the response was written.
 */
bool board_commands_get(shell_t *shell, const char *item);

/**
 * @brief Set a board property.
 * @param shell Shell session.
 * @param item Property name.
 * @param value Requested property value.
 * @return True when the operation completed and its response was written.
 */
bool board_commands_set(shell_t *shell, const char *item, const char *value);

/**
 * @brief Read or update startup configuration.
 * @param context Shell session.
 * @param argc Argument count including the `config` group.
 * @param argv Command tokens.
 * @return True when the operation completed and its response was written.
 */
bool board_commands_config(void *context, int argc, const char *const *argv);

/**
 * @brief Schedule a board reboot after acknowledging the request.
 * @param shell Shell session.
 * @return True when the reboot response was written.
 */
bool board_commands_reboot(shell_t *shell);

#endif
