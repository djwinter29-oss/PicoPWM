/**
 * @file boot_config.c
 * @brief Boot settings storage: one CRC-protected flash record plus a RAM copy.
 */

#include "boot_config.h"

#include "platform/system.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

#include <stddef.h>
#include <string.h>

#define BOOT_CONFIG_MAGIC 0x50434F4Eu
#define BOOT_CONFIG_VERSION 3u
#ifndef PICO_PWM_CONFIG_FLASH_OFFSET
#error "PICO_PWM_CONFIG_FLASH_OFFSET must be provided by the firmware build"
#endif
#define BOOT_CONFIG_FLASH_OFFSET PICO_PWM_CONFIG_FLASH_OFFSET

/** @brief Settings applied at boot. */
typedef struct {
    pwm_role_t roles[PWM_BANK_COUNT]; /**< Role per bank. */
    uint8_t i2c_address;              /**< 7-bit I2C slave address. */
    boot_config_sysclk_t sysclk;      /**< System clock profile. */
} boot_config_t;

static boot_config_t current; /**< RAM copy; edited by the setters and persisted by save. */

/**
 * @brief On-flash record, written and read as raw little-endian memory.
 *
 * ponytail: one sector, so a power loss during the erase falls back to defaults. The old two-slot scheme kept the
 * previous record; a settings-only record that is re-entered over USB does not justify the extra sector.
 */
typedef struct {
    uint32_t magic;                /**< BOOT_CONFIG_MAGIC. */
    uint16_t version;              /**< BOOT_CONFIG_VERSION. */
    uint16_t size;                 /**< sizeof(boot_config_record_t). */
    uint8_t roles[PWM_BANK_COUNT]; /**< Bank roles indexed by bank. */
    uint8_t i2c_address;           /**< 7-bit I2C slave address. */
    uint8_t sysclk;                /**< boot_config_sysclk_t value. */
    uint8_t reserved[3];           /**< Zero; pads the CRC to a 4-byte boundary. */
    uint32_t crc;                  /**< IEEE CRC-32 of all preceding bytes. */
} boot_config_record_t;

_Static_assert(sizeof(boot_config_record_t) == 20u && offsetof(boot_config_record_t, crc) == 16u, "record layout");

/** @brief IEEE 802.3 CRC-32 (reflected, init and final XOR 0xFFFFFFFF). */
static uint32_t boot_config_crc32(const void *data, size_t size) {
    const uint8_t *bytes = data;
    uint32_t crc = 0xffffffffu;

    while (size-- > 0u) {
        crc ^= *bytes++;
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static const boot_config_record_t *boot_config_stored(void) {
    return (const boot_config_record_t *)(XIP_BASE + BOOT_CONFIG_FLASH_OFFSET);
}

static bool boot_config_record_valid(const boot_config_record_t *record) {
    return record->magic == BOOT_CONFIG_MAGIC && record->version == BOOT_CONFIG_VERSION &&
           record->size == sizeof(*record) &&
           record->crc == boot_config_crc32(record, offsetof(boot_config_record_t, crc));
}

static void boot_config_default(boot_config_t *config) {
    *config = (boot_config_t){
        .roles = {PWM_ROLE_MONITOR, PWM_ROLE_MONITOR, PWM_ROLE_MONITOR},
        .i2c_address = 0x40u,
    };
}

static bool boot_config_valid(const boot_config_t *config) {
    for (unsigned bank = 0u; bank < PWM_BANK_COUNT; ++bank) {
        if (config->roles[bank] > PWM_ROLE_MONITOR)
            return false;
    }
    return (unsigned)config->sysclk <= BOOT_CONFIG_SYSCLK_250MHZ && config->i2c_address >= 0x08u &&
           config->i2c_address <= 0x77u;
}

static bool boot_config_read_saved(boot_config_t *config) {
    const boot_config_record_t *record = boot_config_stored();
    boot_config_t candidate;

    boot_config_default(&candidate);
    if (boot_config_record_valid(record)) {
        for (unsigned bank = 0u; bank < PWM_BANK_COUNT; ++bank)
            candidate.roles[bank] = (pwm_role_t)record->roles[bank];
        candidate.i2c_address = record->i2c_address;
        candidate.sysclk = (boot_config_sysclk_t)record->sysclk;
        if (boot_config_valid(&candidate)) {
            *config = candidate;
            return true;
        }
        boot_config_default(&candidate);
    }
    *config = candidate;
    return false;
}

bool boot_config_load(void) {
    return boot_config_read_saved(&current);
}

bool boot_config_save(void) {
    boot_config_record_t record = {0};
    static uint8_t sector[FLASH_SECTOR_SIZE];

    /* Core 1 must already be the lockout victim. Starting the lockout without it blocks forever. */
    if (!multicore_lockout_victim_is_initialized(1u)) {
        return false;
    }
    record.magic = BOOT_CONFIG_MAGIC;
    record.version = BOOT_CONFIG_VERSION;
    record.size = sizeof(record);
    for (unsigned bank = 0u; bank < PWM_BANK_COUNT; ++bank)
        record.roles[bank] = (uint8_t)current.roles[bank];
    record.i2c_address = current.i2c_address;
    record.sysclk = (uint8_t)current.sysclk;
    record.crc = boot_config_crc32(&record, offsetof(boot_config_record_t, crc));
    if (memcmp(boot_config_stored(), &record, sizeof(record)) == 0) {
        return true;
    }
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
    flash_range_erase(BOOT_CONFIG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(BOOT_CONFIG_FLASH_OFFSET, sector, FLASH_SECTOR_SIZE);
    multicore_lockout_end_blocking();
    restore_interrupts(irq_state);
    return memcmp(boot_config_stored(), &record, sizeof(record)) == 0;
}

pwm_role_t boot_config_get_bank(pwm_bank_t bank) {
    return bank < PWM_BANK_COUNT ? current.roles[bank] : PWM_ROLE_MONITOR;
}

bool boot_config_set_bank(pwm_bank_t bank, pwm_role_t role) {
    if (bank >= PWM_BANK_COUNT || role > PWM_ROLE_MONITOR)
        return false;
    current.roles[bank] = role;
    return true;
}

uint8_t boot_config_get_i2c_address(void) {
    return current.i2c_address;
}

bool boot_config_set_i2c_address(uint8_t address) {
    if (address < 0x08u || address > 0x77u)
        return false;
    current.i2c_address = address;
    return true;
}

boot_config_sysclk_t boot_config_get_sysclk(void) {
    return current.sysclk;
}

bool boot_config_set_sysclk(boot_config_sysclk_t profile) {
    if ((unsigned)profile > BOOT_CONFIG_SYSCLK_250MHZ)
        return false;
    current.sysclk = profile;
    return true;
}
