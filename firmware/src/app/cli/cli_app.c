/**
 * @file cli_app.c
 * @brief CDC command-line application lifecycle.
 */

#include "app/cli/cli_app.h"

#include "app/usb/usb_cdc.h"

#include <stddef.h>
#include <string.h>

/** @brief Explicit error reported when the application shell cannot initialize. */
static const char cli_app_init_error[] = "ERR CDC CLI initialization failed\r\n";

/** @brief CDC byte transport used by the command shell. */
static const shell_transport_t cli_app_transport = {
    .read = usb_cdc_read,
    .write = usb_cdc_write,
    .context = NULL,
};

/** @brief Reset shell state when a disconnect transition is observed. */
static bool cli_app_handle_disconnect(cli_app_t *app, bool connected) {
    uint32_t disconnects = usb_cdc_disconnect_count();
    bool disconnected = (disconnects != app->handled_disconnects) || (!connected && app->was_connected);

    app->was_connected = connected;
    if (!disconnected) {
        return false;
    }

    app->handled_disconnects = disconnects;
    usb_cdc_reset_session();
    app->init_error_pending = !pwm_commands_on_disconnected(&app->commands);
    usb_cdc_acknowledge_session_reset();
    app->connection_output_complete = false;
    return true;
}

bool cli_app_init(cli_app_t *app) {
    if (app == NULL) {
        return false;
    }

    memset(app, 0, sizeof(*app));
    app->init_error_pending = !pwm_commands_init(&app->commands, &cli_app_transport);
    return !app->init_error_pending;
}

void cli_app_poll(cli_app_t *app) {
    bool connected;

    if (app == NULL) {
        return;
    }

    connected = usb_cdc_is_connected();
    (void)cli_app_handle_disconnect(app, connected);

    if (connected && !app->init_error_pending && !app->connection_output_complete) {
        app->connection_output_complete = pwm_commands_on_connected(&app->commands);
    } else if (connected && app->init_error_pending &&
               usb_cdc_write(NULL, (const uint8_t *)cli_app_init_error, sizeof(cli_app_init_error) - 1u)) {
        app->init_error_pending = false;
    }

    if (connected && !app->init_error_pending) {
        pwm_commands_poll(&app->commands);
    }
}
