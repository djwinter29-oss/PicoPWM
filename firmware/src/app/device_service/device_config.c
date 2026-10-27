/**
 * @file device_config.c
 * @brief Device settings storage: two CRC-protected flash slots plus running and target copies.
 */

#include "device_config.h"

#include "platform/system.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

#include <stddef.h>
#include <string.h>

#define DEVICE_CONFIG_MAGIC 0x50434F4Eu
#define DEVICE_CONFIG_VERSION 2u
#ifndef PICO_PWM_CONFIG_FLASH_OFFSET
#error "PICO_PWM_CONFIG_FLASH_OFFSET must be provided by the firmware build"
#endif
#define DEVICE_CONFIG_FLASH_OFFSET PICO_PWM_CONFIG_FLASH_OFFSET

static device_config_t device_config_running;
static device_config_t device_config_target;

/** @brief On-flash record; the newer valid generation of the two slots wins. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t roles[PWM_DRIVER_CONFIG_BANK_COUNT];
    uint8_t i2c_address;
    uint32_t generation;
    uint32_t crc;
} device_config_record_t;

static uint32_t device_config_record_checksum(const device_config_record_t *record) {
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t hash = 2166136261u;

    for (size_t i = 0u; i < offsetof(device_config_record_t, crc); ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool device_config_record_valid(const device_config_record_t *record) {
    return record->magic == DEVICE_CONFIG_MAGIC && record->version == DEVICE_CONFIG_VERSION &&
           record->crc == device_config_record_checksum(record);
}

void device_config_default(device_config_t *config) {
    if (config == NULL)
        return;
    *config = (device_config_t){
        .bank_a_role = PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
        .bank_b_role = PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
        .bank_c_role = PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
        .i2c_address = 0x40u,
    };
}

bool device_config_validate(const device_config_t *config) {
    if (config == NULL)
        return false;
    return (config->bank_a_role <= PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR) &&
           (config->bank_b_role <= PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR) &&
           (config->bank_c_role <= PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR) && (config->i2c_address >= 0x08u) &&
           (config->i2c_address <= 0x77u);
}

bool device_config_load_target(device_config_t *config) {
    device_config_t candidate;

    if (config == NULL)
        return false;
    device_config_default(&candidate);
    const device_config_record_t *first = (const device_config_record_t *)(XIP_BASE + DEVICE_CONFIG_FLASH_OFFSET);
    const device_config_record_t *second =
        (const device_config_record_t *)(XIP_BASE + DEVICE_CONFIG_FLASH_OFFSET + FLASH_SECTOR_SIZE);
    bool first_valid = device_config_record_valid(first);
    bool second_valid = device_config_record_valid(second);
    if (!first_valid && !second_valid) {
        *config = candidate;
        return false;
    }

    const device_config_record_t *record =
        (first_valid && (!second_valid || (int32_t)(first->generation - second->generation) >= 0)) ? first : second;

    candidate.bank_a_role = (pwm_driver_config_bank_role_t)record->roles[PWM_DRIVER_CONFIG_BANK_A];
    candidate.bank_b_role = (pwm_driver_config_bank_role_t)record->roles[PWM_DRIVER_CONFIG_BANK_B];
    candidate.bank_c_role = (pwm_driver_config_bank_role_t)record->roles[PWM_DRIVER_CONFIG_BANK_C];
    candidate.i2c_address = record->i2c_address;
    if (!device_config_validate(&candidate)) {
        device_config_default(config);
        return false;
    }

    *config = candidate;
    return true;
}

bool device_config_save_target(const device_config_t *config) {
    device_config_record_t record = {0};
    static uint8_t sector[FLASH_SECTOR_SIZE];
    const device_config_record_t *first;
    const device_config_record_t *second;
    uint32_t slot_offset;

    if ((config == NULL) || !device_config_validate(config))
        return false;
    /* Core 1 must already be the lockout victim. Starting the lockout without it blocks forever. */
    if (!multicore_lockout_victim_is_initialized(1u)) {
        return false;
    }
    record.magic = DEVICE_CONFIG_MAGIC;
    record.version = DEVICE_CONFIG_VERSION;
    record.roles[PWM_DRIVER_CONFIG_BANK_A] = (uint8_t)config->bank_a_role;
    record.roles[PWM_DRIVER_CONFIG_BANK_B] = (uint8_t)config->bank_b_role;
    record.roles[PWM_DRIVER_CONFIG_BANK_C] = (uint8_t)config->bank_c_role;
    record.i2c_address = config->i2c_address;
    first = (const device_config_record_t *)(XIP_BASE + DEVICE_CONFIG_FLASH_OFFSET);
    second = (const device_config_record_t *)(XIP_BASE + DEVICE_CONFIG_FLASH_OFFSET + FLASH_SECTOR_SIZE);
    bool first_valid = device_config_record_valid(first);
    bool second_valid = device_config_record_valid(second);
    bool first_is_newer = first_valid && (!second_valid || (int32_t)(first->generation - second->generation) >= 0);
    record.generation = first_valid && second_valid ? (first_is_newer ? first->generation : second->generation) + 1u
                        : first_valid               ? first->generation + 1u
                        : second_valid              ? second->generation + 1u
                                                    : 1u;
    record.crc = device_config_record_checksum(&record);
    slot_offset = first_is_newer ? DEVICE_CONFIG_FLASH_OFFSET + FLASH_SECTOR_SIZE : DEVICE_CONFIG_FLASH_OFFSET;
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

bool device_config_save(void) {
    return device_config_save_target(&device_config_target);
}

bool device_config_target_dirty(void) {
    device_config_t saved;

    (void)device_config_load_target(&saved);
    return device_config_target.bank_a_role != saved.bank_a_role ||
           device_config_target.bank_b_role != saved.bank_b_role ||
           device_config_target.bank_c_role != saved.bank_c_role ||
           device_config_target.i2c_address != saved.i2c_address;
}

bool device_config_init_state(const device_config_t *running, const device_config_t *target) {
    if (!device_config_validate(running) || !device_config_validate(target)) {
        return false;
    }

    device_config_running = *running;
    device_config_target = *target;
    return true;
}

bool device_config_get_target(device_config_t *config) {
    if (config == NULL)
        return false;
    *config = device_config_target;
    return true;
}

bool device_config_get_running(device_config_t *config) {
    if (config == NULL)
        return false;
    *config = device_config_running;
    return true;
}

bool device_config_set_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_role_t role) {
    device_config_t candidate = device_config_target;
    pwm_driver_config_bank_role_t *slots[PWM_DRIVER_CONFIG_BANK_COUNT] = {
        &candidate.bank_a_role, &candidate.bank_b_role, &candidate.bank_c_role};

    if ((bank >= PWM_DRIVER_CONFIG_BANK_COUNT) || (role > PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR))
        return false;
    *slots[bank] = role;
    device_config_target = candidate;
    return true;
}

bool device_config_set_i2c_address(uint8_t address) {
    if ((address < 0x08u) || (address > 0x77u))
        return false;
    device_config_target.i2c_address = address;
    return true;
}
