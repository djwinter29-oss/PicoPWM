/**
 * @file pwm_driver_config.c
 * @brief Startup PWM driver configuration, lookup, and validation helpers.
 */

#include "pwm_driver_config.h"
#include "pwm_driver.h"

#include "platform/system.h"
#include "hardware/pwm.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

#include <stddef.h>
#include <string.h>

#define PWM_DRIVER_CONFIG_MAGIC 0x50434F4Eu
#define PWM_DRIVER_CONFIG_VERSION 2u
#define PWM_DRIVER_CONFIG_SLOT_COUNT 2u
#ifndef PICO_PWM_CONFIG_FLASH_OFFSET
#error "PICO_PWM_CONFIG_FLASH_OFFSET must be provided by the firmware build"
#endif
#define PWM_DRIVER_CONFIG_FLASH_OFFSET PICO_PWM_CONFIG_FLASH_OFFSET
static pwm_driver_config_t pwm_driver_config_running;
static pwm_driver_config_t pwm_driver_config_target;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t roles[PWM_DRIVER_CONFIG_BANK_COUNT];
    uint8_t i2c_address;
    uint32_t generation;
    uint32_t crc;
} pwm_driver_config_record_t;

static uint32_t pwm_driver_config_record_checksum(const pwm_driver_config_record_t *record) {
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t hash = 2166136261u;

    for (size_t i = 0u; i < offsetof(pwm_driver_config_record_t, crc); ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

void pwm_driver_config_default(pwm_driver_config_t *config) {
    if (config == NULL)
        return;
    *config = (pwm_driver_config_t){
        .bank_a_role = PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
        .bank_b_role = PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
        .bank_c_role = PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
        .i2c_address = 0x40u,
    };
}

bool pwm_driver_config_validate_target(const pwm_driver_config_t *config) {
    if (config == NULL)
        return false;
    /* Each bank is pinned to one backend; only its role is selectable. */
    return (config->bank_a_role <= PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR) &&
           (config->bank_b_role <= PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR) &&
           (config->bank_c_role <= PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR) && (config->i2c_address >= 0x08u) &&
           (config->i2c_address <= 0x77u);
}

bool pwm_driver_config_load_target(pwm_driver_config_t *config) {
    const pwm_driver_config_record_t *record;
    pwm_driver_config_t candidate;

    if (config == NULL)
        return false;
    pwm_driver_config_default(&candidate);
    const pwm_driver_config_record_t *first =
        (const pwm_driver_config_record_t *)(XIP_BASE + PWM_DRIVER_CONFIG_FLASH_OFFSET);
    const pwm_driver_config_record_t *second =
        (const pwm_driver_config_record_t *)(XIP_BASE + PWM_DRIVER_CONFIG_FLASH_OFFSET + FLASH_SECTOR_SIZE);
    bool first_valid = first->magic == PWM_DRIVER_CONFIG_MAGIC && first->version == PWM_DRIVER_CONFIG_VERSION &&
                       first->crc == pwm_driver_config_record_checksum(first);
    bool second_valid = second->magic == PWM_DRIVER_CONFIG_MAGIC && second->version == PWM_DRIVER_CONFIG_VERSION &&
                        second->crc == pwm_driver_config_record_checksum(second);
    if (!first_valid && !second_valid) {
        *config = candidate;
        return false;
    }

    record =
        (first_valid && (!second_valid || (int32_t)(first->generation - second->generation) >= 0)) ? first : second;

    candidate.bank_a_role = (pwm_driver_config_bank_role_t)record->roles[PWM_DRIVER_CONFIG_BANK_A];
    candidate.bank_b_role = (pwm_driver_config_bank_role_t)record->roles[PWM_DRIVER_CONFIG_BANK_B];
    candidate.bank_c_role = (pwm_driver_config_bank_role_t)record->roles[PWM_DRIVER_CONFIG_BANK_C];
    candidate.i2c_address = record->i2c_address;
    if (!pwm_driver_config_validate_target(&candidate)) {
        *config = candidate;
        pwm_driver_config_default(config);
        return false;
    }

    *config = candidate;
    return true;
}

bool pwm_driver_config_save_target(const pwm_driver_config_t *config) {
    pwm_driver_config_record_t record = {0};
    static uint8_t sector[FLASH_SECTOR_SIZE];
    const pwm_driver_config_record_t *first;
    const pwm_driver_config_record_t *second;
    uint32_t slot_offset;

    if ((config == NULL) || !pwm_driver_config_validate_target(config))
        return false;
    /* Core 1 must already be the lockout victim. Starting the lockout without it blocks forever. */
    if (!multicore_lockout_victim_is_initialized(1u)) {
        return false;
    }
    record.magic = PWM_DRIVER_CONFIG_MAGIC;
    record.version = PWM_DRIVER_CONFIG_VERSION;
    record.roles[PWM_DRIVER_CONFIG_BANK_A] = (uint8_t)config->bank_a_role;
    record.roles[PWM_DRIVER_CONFIG_BANK_B] = (uint8_t)config->bank_b_role;
    record.roles[PWM_DRIVER_CONFIG_BANK_C] = (uint8_t)config->bank_c_role;
    record.i2c_address = config->i2c_address;
    first = (const pwm_driver_config_record_t *)(XIP_BASE + PWM_DRIVER_CONFIG_FLASH_OFFSET);
    second = (const pwm_driver_config_record_t *)(XIP_BASE + PWM_DRIVER_CONFIG_FLASH_OFFSET + FLASH_SECTOR_SIZE);
    bool first_valid = first->magic == PWM_DRIVER_CONFIG_MAGIC && first->version == PWM_DRIVER_CONFIG_VERSION &&
                       first->crc == pwm_driver_config_record_checksum(first);
    bool second_valid = second->magic == PWM_DRIVER_CONFIG_MAGIC && second->version == PWM_DRIVER_CONFIG_VERSION &&
                        second->crc == pwm_driver_config_record_checksum(second);
    bool first_is_newer = first_valid && (!second_valid || (int32_t)(first->generation - second->generation) >= 0);
    record.generation = first_valid && second_valid ? (first_is_newer ? first->generation : second->generation) + 1u
                        : first_valid               ? first->generation + 1u
                        : second_valid              ? second->generation + 1u
                                                    : 1u;
    record.crc = pwm_driver_config_record_checksum(&record);
    slot_offset = first_is_newer ? PWM_DRIVER_CONFIG_FLASH_OFFSET + FLASH_SECTOR_SIZE : PWM_DRIVER_CONFIG_FLASH_OFFSET;
    memset(sector, 0xff, sizeof(sector));
    memcpy(sector, &record, sizeof(record));
    /*
     * Pico SDK 2.3.0 flash_range_* does not disable interrupts or pause the other core.
     * The vector table and IRQ handlers live in flash, so Core 0 interrupts stay off and
     * Core 1 stays locked out for the whole erase and program. Lockout is not recursive.
     * Core 1 must already have called multicore_lockout_victim_init().
     */
    system_watchdog_kick();
    uint32_t irq_state = save_and_disable_interrupts();
    multicore_lockout_start_blocking();
    flash_range_erase(slot_offset, FLASH_SECTOR_SIZE);
    flash_range_program(slot_offset, sector, FLASH_SECTOR_SIZE);
    multicore_lockout_end_blocking();
    restore_interrupts(irq_state);
    return memcmp((const void *)(uintptr_t)(XIP_BASE + slot_offset), &record, sizeof(record)) == 0;
}

void pwm_driver_config_arm_lockout_victim(void) {
    multicore_lockout_victim_init();
}

bool pwm_driver_config_lockout_victim_ready(void) {
    return multicore_lockout_victim_is_initialized(1u);
}

bool pwm_driver_config_init_state(const pwm_driver_config_t *running, const pwm_driver_config_t *target) {
    if ((running == NULL) || (target == NULL) || !pwm_driver_config_validate_target(running) ||
        !pwm_driver_config_validate_target(target)) {
        return false;
    }

    pwm_driver_config_running = *running;
    pwm_driver_config_target = *target;
    return true;
}

bool pwm_driver_config_get_target(pwm_driver_config_t *config) {
    if (config == NULL)
        return false;
    *config = pwm_driver_config_target;
    return true;
}

bool pwm_driver_config_get_running(pwm_driver_config_t *config) {
    if (config == NULL)
        return false;
    *config = pwm_driver_config_running;
    return true;
}

bool pwm_driver_config_set_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_role_t role) {
    pwm_driver_config_t candidate = pwm_driver_config_target;
    if ((bank >= PWM_DRIVER_CONFIG_BANK_COUNT) || (role > PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR))
        return false;
    switch (bank) {
    case PWM_DRIVER_CONFIG_BANK_A:
        candidate.bank_a_role = role;
        break;
    case PWM_DRIVER_CONFIG_BANK_B:
        candidate.bank_b_role = role;
        break;
    case PWM_DRIVER_CONFIG_BANK_C:
        candidate.bank_c_role = role;
        break;
    default:
        return false;
    }
    if (!pwm_driver_config_validate_target(&candidate))
        return false;
    pwm_driver_config_target = candidate;
    return true;
}

bool pwm_driver_config_set_i2c_address(uint8_t address) {
    pwm_driver_config_t candidate = pwm_driver_config_target;
    if ((address < 0x08u) || (address > 0x77u))
        return false;
    candidate.i2c_address = address;
    pwm_driver_config_target = candidate;
    return true;
}

/** @brief Bank roles chosen at startup; set once before Core 1 launches. */
static pwm_driver_config_bank_role_t pwm_driver_config_bank_roles[PWM_DRIVER_CONFIG_BANK_COUNT];
static bool pwm_driver_config_table_configured = false;

/** @brief Return the board GPIO for one channel of a bank. */
static uint pwm_driver_config_bank_gpio(pwm_driver_config_bank_t bank, uint index) {
    switch (bank) {
    case PWM_DRIVER_CONFIG_BANK_A:
        return hardware_pwm_config.channels[index].gpio;
    case PWM_DRIVER_CONFIG_BANK_B:
        return pio_pwm_config.channels[index].gpio;
    default:
        return software_pwm_config.channels[index].gpio;
    }
}

bool pwm_driver_config_channel_role(uint channel, pwm_driver_config_bank_role_t *role) {
    if (!pwm_driver_config_table_configured || (role == NULL) || (channel >= PWM_DRIVER_CONFIG_CHANNEL_COUNT)) {
        return false;
    }
    *role = pwm_driver_config_bank_roles[channel / PWM_DRIVER_CONFIG_BANK_SIZE];
    return true;
}

const char *pwm_driver_config_channel_name(uint channel) {
    static const char *const names[PWM_DRIVER_CONFIG_BANK_COUNT][2] = {
        {"HW", "HW-MON"},
        {"PIO", "PIO-MON"},
        {"SW", "SW-MON"},
    };
    pwm_driver_config_bank_role_t role;

    return pwm_driver_config_channel_role(channel, &role) ? names[channel / PWM_DRIVER_CONFIG_BANK_SIZE][role] : "?";
}

bool pwm_driver_config_validate(void) {
    uint32_t used = 0u;

    for (pwm_driver_config_bank_t bank = PWM_DRIVER_CONFIG_BANK_A; bank < PWM_DRIVER_CONFIG_BANK_COUNT; ++bank) {
        for (uint i = 0u; i < PWM_DRIVER_CONFIG_BANK_SIZE; ++i) {
            uint gpio = pwm_driver_config_bank_gpio(bank, i);

            if (gpio >= PWM_DRIVER_CONFIG_GPIO_COUNT || (used & (1u << gpio)) != 0u) {
                return false;
            }
            used |= 1u << gpio;

            if ((gpio == 23u || gpio == 24u) && bank != PWM_DRIVER_CONFIG_BANK_C) {
                return false;
            }
            if (PWM_DRIVER_CONFIG_REQUIRE_HW_CHANNEL_B && bank == PWM_DRIVER_CONFIG_BANK_A &&
                pwm_gpio_to_channel(gpio) != PWM_CHAN_B) {
                return false;
            }
        }
    }

    return true;
}

bool pwm_driver_config_frequency_supported(uint channel, uint32_t frequency_hz) {
    pwm_driver_config_bank_role_t role;
    uint32_t min_hz = 1u;
    uint32_t max_hz = 1000000u;

    if (!pwm_driver_config_channel_role(channel, &role)) {
        return false;
    }
    if (frequency_hz == 0u) {
        return true;
    }

    if (channel / PWM_DRIVER_CONFIG_BANK_SIZE == PWM_DRIVER_CONFIG_BANK_A && role == PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR) {
        min_hz = 10u;
    } else if (channel / PWM_DRIVER_CONFIG_BANK_SIZE == PWM_DRIVER_CONFIG_BANK_C) {
        max_hz = 1000u;
    }

    return frequency_hz >= min_hz && frequency_hz <= max_hz;
}

bool pwm_driver_config_is_monitor(void) {
    for (pwm_driver_config_bank_t bank = PWM_DRIVER_CONFIG_BANK_A; bank < PWM_DRIVER_CONFIG_BANK_COUNT; ++bank) {
        if (pwm_driver_config_table_configured &&
            pwm_driver_config_bank_roles[bank] == PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR) {
            return true;
        }
    }
    return false;
}

bool pwm_driver_configure_table(const pwm_driver_config_bank_role_t roles[PWM_DRIVER_CONFIG_BANK_COUNT]) {
    if ((roles == NULL) || pwm_driver_config_table_configured) {
        return false;
    }

    for (pwm_driver_config_bank_t bank = PWM_DRIVER_CONFIG_BANK_A; bank < PWM_DRIVER_CONFIG_BANK_COUNT; ++bank) {
        if ((roles[bank] != PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR) &&
            (roles[bank] != PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR)) {
            return false;
        }
    }

    memcpy(pwm_driver_config_bank_roles, roles, sizeof(pwm_driver_config_bank_roles));
    pwm_driver_config_table_configured = true;
    if (!pwm_driver_config_validate()) {
        pwm_driver_config_table_configured = false;
        return false;
    }
    return true;
}
