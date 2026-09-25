/**
 * @file pwm_commands.h
 * @brief PicoPWM device commands layered on top of the CLI editor.
 */

#ifndef PWM_COMMANDS_H
#define PWM_COMMANDS_H

#include "cli/shell.h"

/**
 * @brief Initialize the PicoPWM device commands using the supplied transport.
 * @param transport Caller-owned shell transport binding.
 */
void pwm_commands_init(const shell_transport_t *transport);

/** @brief Handle one transport connection event by printing the initial command help. */
void pwm_commands_on_connected(void);

/** @brief Poll the device command transport and dispatch complete commands. */
void pwm_commands_poll(void);

#endif