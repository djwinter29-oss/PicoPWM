/**
 * @file cli_board_commands.c
 * @brief Board and startup-configuration command handlers for the CDC shell.
 */

#include "app/cli/cli_board_commands.h"

#include "app/cli/cli_syntax.h"
#include "device.h"
#include "platform/boot_config.h"
#include "platform/system.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMAND_SHELL ((shell_t *)context)

/** @brief Staged configuration changes; nothing is written until every field validates. */
typedef struct {
    bool bank_changed[PWM_BANK_COUNT];    /**< Whether the corresponding bank role was requested. */
    pwm_role_t bank_role[PWM_BANK_COUNT]; /**< New role for each requested bank. */
    bool clock_changed;                   /**< Whether the system clock profile was requested. */
    boot_config_sysclk_t clock_profile;   /**< New system clock profile. */
    bool address_changed;                 /**< Whether the I2C address was requested. */
    uint8_t address;                      /**< New 7-bit I2C address. */
} board_config_update_t;

/** @brief Map a bank role to its user-facing name. */
static const char *board_commands_role_name(pwm_role_t role) {
    return role == PWM_ROLE_GENERATOR ? "generator" : "monitor";
}

/** @brief Parse a decimal unsigned integer without signs or trailing characters. */
static bool board_commands_parse_decimal(const char *text, unsigned long *value_out) {
    char *end = NULL;
    unsigned long value;

    if ((text == NULL) || (text[0] < '0') || (text[0] > '9') || (value_out == NULL)) {
        return false;
    }

    errno = 0;
    value = strtoul(text, &end, 10);
    if ((errno == ERANGE) || (end == text) || (end == NULL) || (*end != '\0')) {
        return false;
    }

    *value_out = value;
    return true;
}

/** @brief Print one setting's active and saved-target values. */
static bool board_commands_config_item(shell_t *shell, const char *item) {
    char line[96];
    unsigned long running_clock = (unsigned long)device_system_clock_hz();
    uint16_t target_clock = boot_config_get_sysclk_mhz();

    if (strcmp(item, "banka") == 0) {
        snprintf(line, sizeof(line), "banka running=%s target=%s",
                 board_commands_role_name(device_bank_role(PWM_BANK_A)),
                 board_commands_role_name(boot_config_get_bank(PWM_BANK_A)));
    } else if (strcmp(item, "bankb") == 0) {
        snprintf(line, sizeof(line), "bankb running=%s target=%s",
                 board_commands_role_name(device_bank_role(PWM_BANK_B)),
                 board_commands_role_name(boot_config_get_bank(PWM_BANK_B)));
    } else if (strcmp(item, "bankc") == 0) {
        snprintf(line, sizeof(line), "bankc running=%s target=%s",
                 board_commands_role_name(device_bank_role(PWM_BANK_C)),
                 board_commands_role_name(boot_config_get_bank(PWM_BANK_C)));
    } else if (strcmp(item, "clock") == 0) {
        if (target_clock == 0u) {
            snprintf(line, sizeof(line), "clock running=%lu Hz target=default", running_clock);
        } else {
            snprintf(line, sizeof(line), "clock running=%lu Hz target=%u MHz", running_clock, (unsigned)target_clock);
        }
    } else if (strcmp(item, "address") == 0) {
        snprintf(line, sizeof(line), "address running=%u target=%u", (unsigned)device_i2c_address(),
                 (unsigned)boot_config_get_i2c_address());
    } else {
        return shell_write_line(shell, "ERR config item must be banka, bankb, bankc, clock or address");
    }

    return shell_write_line(shell, line);
}

/** @brief Parse and validate one comma-separated configuration assignment. */
static bool board_commands_parse_assignment(char *assignment, board_config_update_t *update) {
    char *equal = strchr(assignment, '=');
    unsigned long number;

    if ((equal == NULL) || (equal == assignment) || (equal[1] == '\0') || strchr(equal + 1, '=') != NULL) {
        return false;
    }
    *equal++ = '\0';

    if (strcmp(assignment, "banka") == 0 || strcmp(assignment, "bankb") == 0 || strcmp(assignment, "bankc") == 0) {
        pwm_bank_t bank = (strcmp(assignment, "banka") == 0)   ? PWM_BANK_A
                          : (strcmp(assignment, "bankb") == 0) ? PWM_BANK_B
                                                               : PWM_BANK_C;
        if (update->bank_changed[bank]) {
            return false;
        }
        if (strcmp(equal, "generator") == 0) {
            update->bank_role[bank] = PWM_ROLE_GENERATOR;
        } else if (strcmp(equal, "monitor") == 0) {
            update->bank_role[bank] = PWM_ROLE_MONITOR;
        } else {
            return false;
        }
        update->bank_changed[bank] = true;
        return true;
    }

    if (strcmp(assignment, "clock") == 0) {
        if (update->clock_changed) {
            return false;
        }
        if (strcmp(equal, "default") == 0) {
            update->clock_profile = BOOT_CONFIG_SYSCLK_DEFAULT;
        } else {
            if (!board_commands_parse_decimal(equal, &number)) {
                return false;
            }
            if (number == 150u) {
                update->clock_profile = BOOT_CONFIG_SYSCLK_150MHZ;
            } else if (number == 200u) {
                update->clock_profile = BOOT_CONFIG_SYSCLK_200MHZ;
            } else if (number == 250u) {
                update->clock_profile = BOOT_CONFIG_SYSCLK_250MHZ;
            } else {
                return false;
            }
        }
        update->clock_changed = true;
        return true;
    }

    if (strcmp(assignment, "address") == 0) {
        if (update->address_changed || !board_commands_parse_decimal(equal, &number) || (number < 8u) ||
            (number > 119u)) {
            return false;
        }
        update->address = (uint8_t)number;
        update->address_changed = true;
        return true;
    }

    return false;
}

/** @brief Parse the complete update before mutating any stored setting. */
static bool board_commands_parse_update(int argc, const char *const *argv, board_config_update_t *update) {
    char assignments[96];
    size_t used = 0u;

    if ((argc < 3) || (update == NULL)) {
        return false;
    }

    assignments[0] = '\0';
    for (int index = 2; index < argc; ++index) {
        size_t length = strlen(argv[index]);
        if ((used + length + (used != 0u ? 1u : 0u)) >= sizeof(assignments)) {
            return false;
        }
        if (used != 0u) {
            assignments[used++] = ' ';
        }
        memcpy(&assignments[used], argv[index], length);
        used += length;
        assignments[used] = '\0';
    }

    char *cursor = assignments;
    while (*cursor != '\0') {
        char *end = strchr(cursor, ',');
        char *first;
        char *last;

        if (end != NULL) {
            *end = '\0';
        }
        first = cursor;
        while (isspace((unsigned char)*first)) {
            ++first;
        }
        last = first + strlen(first);
        while ((last > first) && isspace((unsigned char)last[-1])) {
            *--last = '\0';
        }
        if ((first == last) || !board_commands_parse_assignment(first, update)) {
            return false;
        }
        if (end == NULL) {
            break;
        }
        cursor = end + 1;
        if (*cursor == '\0') {
            return false;
        }
    }

    return update->bank_changed[0] || update->bank_changed[1] || update->bank_changed[2] || update->clock_changed ||
           update->address_changed;
}

/** @brief Restore the staged configuration after a failed flash save. */
static void board_commands_restore_config(const pwm_role_t *roles, boot_config_sysclk_t clock, uint8_t address) {
    for (pwm_bank_t bank = PWM_BANK_A; bank < PWM_BANK_COUNT; bank = (pwm_bank_t)(bank + 1)) {
        (void)boot_config_set_bank(bank, roles[bank]);
    }
    (void)boot_config_set_sysclk(clock);
    (void)boot_config_set_i2c_address(address);
}

/** @brief Commit a fully validated update, saving it and scheduling its reboot. */
static bool board_commands_apply_update(shell_t *shell, const board_config_update_t *update) {
    pwm_role_t previous_roles[PWM_BANK_COUNT];
    boot_config_sysclk_t previous_clock = boot_config_get_sysclk();
    uint8_t previous_address = boot_config_get_i2c_address();

    for (pwm_bank_t bank = PWM_BANK_A; bank < PWM_BANK_COUNT; bank = (pwm_bank_t)(bank + 1)) {
        previous_roles[bank] = boot_config_get_bank(bank);
    }
    for (pwm_bank_t bank = PWM_BANK_A; bank < PWM_BANK_COUNT; bank = (pwm_bank_t)(bank + 1)) {
        if (update->bank_changed[bank] && !boot_config_set_bank(bank, update->bank_role[bank])) {
            board_commands_restore_config(previous_roles, previous_clock, previous_address);
            return shell_write_line(shell, "ERR config update rejected");
        }
    }
    if ((update->clock_changed && !boot_config_set_sysclk(update->clock_profile)) ||
        (update->address_changed && !boot_config_set_i2c_address(update->address))) {
        board_commands_restore_config(previous_roles, previous_clock, previous_address);
        return shell_write_line(shell, "ERR config update rejected");
    }
    if (!boot_config_save()) {
        board_commands_restore_config(previous_roles, previous_clock, previous_address);
        return shell_write_line(shell, "ERR config save failed; settings unchanged");
    }

    if (!shell_write_line(shell, "OK config saved; rebooting")) {
        device_schedule_reboot(500u);
        return false;
    }
    device_schedule_reboot(500u);
    return true;
}

/** @copydoc board_commands_get */
bool board_commands_get(shell_t *shell, const char *item) {
    char line[64];

    if (strcmp(item, "clock") == 0) {
        snprintf(line, sizeof(line), "clock=%lu Hz target=%s", (unsigned long)device_system_clock_hz(),
                 system_clock_is_at_target() ? "met" : "fallback");
        return shell_write_line(shell, line);
    }
    if (strcmp(item, "led") == 0) {
        bool on;
        if (!device_led_read(&on)) {
            return shell_write_line(shell, "ERR LED unavailable");
        }
        return shell_write_line(shell, on ? "led=on" : "led=off");
    }
    if (strcmp(item, "version") == 0) {
        return shell_write_line(shell, device_firmware_version());
    }
    return shell_write_line(shell, "ERR board item must be clock, led or version");
}

/** @copydoc board_commands_set */
bool board_commands_set(shell_t *shell, const char *item, const char *value) {
    bool on;

    if (strcmp(item, "led") != 0) {
        return shell_write_line(shell, "ERR " CLI_SYNTAX_BOARD_SET);
    }
    if (strcmp(value, "on") == 0) {
        on = true;
    } else if (strcmp(value, "off") == 0) {
        on = false;
    } else {
        return shell_write_line(shell, "ERR " CLI_SYNTAX_BOARD_SET);
    }
    if (!device_led_set(on)) {
        return shell_write_line(shell, "ERR LED unavailable");
    }
    return shell_write_line(shell, on ? "OK led on" : "OK led off");
}

/** @copydoc board_commands_config */
bool board_commands_config(void *context, int argc, const char *const *argv) {
    shell_t *shell = COMMAND_SHELL;
    board_config_update_t update = {0};
    const char *const items[] = {"banka", "bankb", "bankc", "clock", "address"};

    if ((argc < 2) || (strcmp(argv[1], "get") != 0 && strcmp(argv[1], "set") != 0)) {
        return shell_write_line(shell, "ERR " CLI_SYNTAX_CONFIG_GET " | " CLI_SYNTAX_CONFIG_SET);
    }
    if (strcmp(argv[1], "get") == 0) {
        if (argc == 2) {
            for (size_t index = 0u; index < sizeof(items) / sizeof(items[0]); ++index) {
                if (!board_commands_config_item(shell, items[index])) {
                    return false;
                }
            }
            return true;
        }
        if ((argc == 3) && (strcmp(argv[2], "?") == 0)) {
            return shell_write_line(shell, "Items: banka, bankb, bankc, clock, address");
        }
        if (argc == 3) {
            return board_commands_config_item(shell, argv[2]);
        }
        return shell_write_line(shell, "ERR " CLI_SYNTAX_CONFIG_GET);
    }

    if ((argc == 2) || ((argc == 3) && (strcmp(argv[2], "?") == 0))) {
        return shell_write_line(shell, CLI_USAGE_CONFIG_SET);
    }
    if (!board_commands_parse_update(argc, argv, &update)) {
        return shell_write_line(shell, "ERR invalid config assignment; use 'config set ?'");
    }
    return board_commands_apply_update(shell, &update);
}

/** @copydoc board_commands_reboot */
bool board_commands_reboot(shell_t *shell) {
    if (!shell_write_line(shell, "OK rebooting")) {
        device_schedule_reboot(500u);
        return false;
    }
    device_schedule_reboot(500u);
    return true;
}
