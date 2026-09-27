#include "cli/board_commands.h"

#include "pwmdriver/channel_config/channel_config.h"
#include "device_api/device_api.h"
#include "board/led.h"
#include "board/system.h"
#include "pwmdriver/pwm_driver.h"

#include <stdio.h>
#include <string.h>

#define COMMAND_SHELL ((shell_t *)context)

static const char *board_commands_result_text(pwm_driver_result_t result) {
    switch (result) {
    case PWM_DRIVER_RESULT_BUSY:
        return "busy";
    case PWM_DRIVER_RESULT_INVALID:
        return "invalid";
    case PWM_DRIVER_RESULT_UNAVAILABLE:
        return "unavailable";
    case PWM_DRIVER_RESULT_TIMEOUT:
        return "timeout";
    case PWM_DRIVER_RESULT_APPLY_FAILED:
        return "apply failed";
    case PWM_DRIVER_RESULT_OK:
    default:
        return "ok";
    }
}

bool board_commands_info(void *context, int argc, const char *const *argv) {
    (void)argv;

    if (argc != 1) {
        return shell_write_line(COMMAND_SHELL, "ERR usage: info");
    }

    return shell_write_line(COMMAND_SHELL, device_api_device_name());
}

bool board_commands_version(void *context, int argc, const char *const *argv) {
    (void)argv;

    if (argc != 1) {
        return shell_write_line(COMMAND_SHELL, "ERR usage: version");
    }

    return shell_write_line(COMMAND_SHELL, device_api_firmware_version());
}

bool board_commands_led(void *context, int argc, const char *const *argv) {
    if (argc != 2) {
        return shell_write_line(COMMAND_SHELL, "ERR usage: led <on|off>");
    }

    if ((strcmp(argv[1], "on") == 0) || (strcmp(argv[1], "1") == 0)) {
        led_set(true);
        return shell_write_line(COMMAND_SHELL, "OK led on");
    }

    if ((strcmp(argv[1], "off") == 0) || (strcmp(argv[1], "0") == 0)) {
        led_set(false);
        return shell_write_line(COMMAND_SHELL, "OK led off");
    }

    return shell_write_line(COMMAND_SHELL, "ERR usage: led <on|off>");
}

bool board_commands_reboot(void *context, int argc, const char *const *argv) {
    (void)argv;

    if (argc != 1) {
        return shell_write_line(COMMAND_SHELL, "ERR usage: reboot");
    }

    shell_write_line(COMMAND_SHELL, "OK rebooting");
    system_reboot();
    return true;
}

bool board_commands_stop(void *context, int argc, const char *const *argv) {
    char line[64];
    pwm_driver_result_t result;

    (void)argv;
    if (argc != 1) {
        return shell_write_line(COMMAND_SHELL, "ERR usage: stop");
    }

    result = device_api_restore_defaults();
    if (result == PWM_DRIVER_RESULT_OK) {
        if (pwm_profile_is_monitor()) {
            return shell_write_line(COMMAND_SHELL, "OK monitor channels unchanged");
        }
        return shell_write_line(COMMAND_SHELL, "OK all channels stopped and reset (freq=0, duty=50%)");
    }

    snprintf(line, sizeof(line), "ERR stop %s", board_commands_result_text(result));
    return shell_write_line(COMMAND_SHELL, line);
}
