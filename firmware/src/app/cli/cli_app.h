// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

/**
 * @file cli_app.h
 * @brief CDC command-line application lifecycle.
 */

#ifndef CLI_APP_H
#define CLI_APP_H

#include "app/cli/cli_pwm_commands.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief CDC shell state and connection lifecycle owned by the CLI application. */
typedef struct {
    pwm_commands_t commands;         /**< Command handlers and interactive shell. */
    bool was_connected;              /**< Connection state observed on the previous poll. */
    bool connection_output_complete; /**< Help and prompt have been accepted by the transport. */
    bool init_error_pending;         /**< Shell initialization error still needs delivery. */
    uint32_t handled_disconnects;    /**< Disconnect transitions whose shell reset has completed. */
} cli_app_t;

/**
 * @brief Initialize the CDC CLI application and its transport binding.
 * @param app Caller-owned application state.
 * @return `true` when the command shell initialized successfully.
 */
bool cli_app_init(cli_app_t *app);

/** @brief Service disconnect transitions, connection output, and shell commands. */
void cli_app_poll(cli_app_t *app);

#endif
