/**
 * @file pwm_commands.h
 * @brief PicoPWM device commands layered on top of the CLI editor.
 */

#ifndef PWM_COMMANDS_H
#define PWM_COMMANDS_H

#include "app/cli/shell.h"

/** @brief CDC command session and connection-output retry state. */
typedef struct {
	shell_t shell; /**< Generic CLI editor and transport state. */
	bool connection_help_queued; /**< Help is queued; only the prompt remains pending after a transient failure. */
} pwm_commands_t;

/**
 * @brief Initialize the PicoPWM device commands using the supplied transport.
 * @param commands Caller-owned CLI command session.
 * @param transport Caller-owned shell transport binding.
 * @return `true` when the shell and command bindings initialized successfully.
 */
bool pwm_commands_init(pwm_commands_t *commands, const shell_transport_t *transport);

/**
 * @brief Print initial command help and prompt, retrying failed output on the next call.
 * @param commands Initialized CLI command session.
 * @return `true` when all connection output was accepted by the transport.
 */
bool pwm_commands_on_connected(pwm_commands_t *commands);

/**
 * @brief Reset connection-output progress after the host disconnects.
 * @param commands CLI command session.
 */
void pwm_commands_on_disconnected(pwm_commands_t *commands);

/** @brief Poll the device command transport and dispatch complete commands. */
void pwm_commands_poll(pwm_commands_t *commands);

#endif