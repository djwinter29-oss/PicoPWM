/**
 * @file device_cli.h
 * @brief PicoPWM command table layered on top of the generic CLI shell.
 */

#ifndef DEVICE_CLI_H
#define DEVICE_CLI_H

#include "cli/cli_shell.h"

/**
 * @brief Initialize the PicoPWM device CLI using the supplied transport.
 * @param transport Caller-owned shell transport binding.
 */
void device_cli_init(const cli_shell_transport_t *transport);

/** @brief Handle one transport connection event by printing the initial CLI help. */
void device_cli_on_connected(void);

/** @brief Poll the device CLI transport and dispatch complete commands. */
void device_cli_poll(void);

#endif