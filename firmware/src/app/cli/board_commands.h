/**
 * @file board_commands.h
 * @brief Board and startup-configuration command handlers for the CDC shell.
 */

#ifndef BOARD_COMMANDS_H
#define BOARD_COMMANDS_H

#include "shell.h"

/**
 * @brief Read one board property.
 * @param context Shell session.
 * @param argc Argument count for `board get <item>`.
 * @param argv Command tokens.
 * @return True when the response was written.
 */
bool board_commands_get(void *context, int argc, const char *const *argv);

/**
 * @brief Set a board property.
 * @param context Shell session.
 * @param argc Argument count for `board set <item> <value>`.
 * @param argv Command tokens.
 * @return True when the operation completed and its response was written.
 */
bool board_commands_set(void *context, int argc, const char *const *argv);

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
 * @param context Shell session.
 * @param argc Argument count.
 * @param argv Command tokens.
 * @return True when the reboot response was written.
 */
bool board_commands_reboot(void *context, int argc, const char *const *argv);

/**
 * @brief Stop all configured generator channels.
 * @param context Shell session.
 * @param argc Argument count.
 * @param argv Command tokens.
 * @return True when the operation completed and its response was written.
 */
bool board_commands_stop(void *context, int argc, const char *const *argv);

#endif
