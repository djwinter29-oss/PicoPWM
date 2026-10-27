#include "app/cli/board_commands.h"

#include "driver/pwm/pwm_driver.h"
#include "app/device_service/device_config.h"
#include "app/device_service/device_service.h"
#include "platform/led.h"
#include "platform/system.h"
#include "driver/pwm/pwm_driver.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
    char line[64];

    (void)argv;

    if (argc != 1) {
        return shell_write_line(COMMAND_SHELL, "ERR usage: info");
    }

    if (!shell_write_line(COMMAND_SHELL, device_service_device_name())) {
        return false;
    }

    snprintf(line, sizeof(line), "clock=%lu Hz target=%s", (unsigned long)system_clock_hz(),
             system_clock_is_at_target() ? "met" : "fallback");
    return shell_write_line(COMMAND_SHELL, line);
}

bool board_commands_version(void *context, int argc, const char *const *argv) {
    (void)argv;

    if (argc != 1) {
        return shell_write_line(COMMAND_SHELL, "ERR usage: version");
    }

    return shell_write_line(COMMAND_SHELL, device_service_firmware_version());
}

static const char *board_commands_role_name(pwm_driver_config_bank_role_t role) {
    return role == PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR ? "gen" : "mon";
}

static void board_commands_format_config(char *line, size_t size, const char *label,
                                         const device_config_t *config) {
    snprintf(line, size, "%s=i2c=0x%02X A:hw/%s B:pio/%s C:sw/%s", label, (unsigned)config->i2c_address,
             board_commands_role_name(config->bank_a_role), board_commands_role_name(config->bank_b_role),
             board_commands_role_name(config->bank_c_role));
}

bool board_commands_config(void *context, int argc, const char *const *argv) {
    char line[128];
    device_config_t running;
    device_config_t target;

    if (argc == 1) {
        device_config_get_running(&running);
        device_config_get_target(&target);
        board_commands_format_config(line, sizeof(line), "running", &running);
        shell_write_line(COMMAND_SHELL, line);
        board_commands_format_config(line, sizeof(line), "target", &target);
        return shell_write_line(COMMAND_SHELL, line);
    }

    if ((argc == 2) && (strcmp(argv[1], "save") == 0)) {
        return shell_write_line(COMMAND_SHELL, device_config_save()
                                                   ? "OK config saved; reboot required"
                                                   : "ERR config invalid or flash write failed");
    }

    if ((argc == 3) && (strcmp(argv[1], "address") == 0)) {
        char *end = NULL;
        unsigned long value = strtoul(argv[2], &end, 0);
        return shell_write_line(COMMAND_SHELL, (end != argv[2] && *end == '\0' && value <= 0x77u && value >= 0x08u &&
                                                device_config_set_i2c_address((uint8_t)value))
                                                   ? "OK target address updated; save then reboot"
                                                   : "ERR invalid I2C address");
    }

    if ((argc != 4) || (strcmp(argv[1], "set") != 0)) {
        return shell_write_line(COMMAND_SHELL,
                                "ERR usage: config [set <a|b|c> <gen|mon>|address <7-bit>|save]");
    }

    pwm_driver_config_bank_t bank = (strcmp(argv[2], "a") == 0)   ? PWM_DRIVER_CONFIG_BANK_A
                                    : (strcmp(argv[2], "b") == 0) ? PWM_DRIVER_CONFIG_BANK_B
                                    : (strcmp(argv[2], "c") == 0) ? PWM_DRIVER_CONFIG_BANK_C
                                                                  : PWM_DRIVER_CONFIG_BANK_COUNT;
    pwm_driver_config_bank_role_t role = (strcmp(argv[3], "gen") == 0)   ? PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR
                                         : (strcmp(argv[3], "mon") == 0) ? PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR
                                                                         : PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR + 1u;

    return shell_write_line(COMMAND_SHELL, device_config_set_bank(bank, role)
                                               ? "OK target updated; save then reboot"
                                               : "ERR invalid bank/role");
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

    result = device_service_restore_defaults();
    if (result == PWM_DRIVER_RESULT_OK) {
        if (pwm_driver_config_is_monitor()) {
            return shell_write_line(COMMAND_SHELL, "OK monitor channels unchanged");
        }
        snprintf(line, sizeof(line), "OK all channels stopped and reset (freq=%u, duty=%u%%)",
                 PWM_DRIVER_STOPPED_FREQ_HZ, (unsigned)PWM_DRIVER_STOPPED_DUTY_PERCENT);
        return shell_write_line(COMMAND_SHELL, line);
    }

    snprintf(line, sizeof(line), "ERR stop %s", board_commands_result_text(result));
    return shell_write_line(COMMAND_SHELL, line);
}
