/**
 * @file pwm_driver_config.c
 * @brief Startup PWM driver configuration, lookup, and validation helpers.
 */

#include "pwm_driver_config.h"
#include "pwm_driver_table.h"
#include "pwm_driver.h"

#include "hardware/pwm.h"
#include "hardware/flash.h"
#include "pico/multicore.h"

#include <stddef.h>
#include <string.h>

#define PWM_DRIVER_CONFIG_MAGIC 0x50434F4Eu
#define PWM_DRIVER_CONFIG_VERSION 1u
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
    uint8_t backends[PWM_DRIVER_CONFIG_BANK_COUNT];
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
        .bank_a_backend = PWM_DRIVER_CONFIG_BANK_BACKEND_HW,
        .bank_b_backend = PWM_DRIVER_CONFIG_BANK_BACKEND_PIO,
        .bank_c_backend = PWM_DRIVER_CONFIG_BANK_BACKEND_SW,
        .bank_a_role = PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
        .bank_b_role = PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
        .bank_c_role = PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
        .i2c_address = 0x40u,
    };
}

bool pwm_driver_config_validate_target(const pwm_driver_config_t *config) {
    if (config == NULL)
        return false;
    return (config->bank_a_backend <= PWM_DRIVER_CONFIG_BANK_BACKEND_SW) &&
           (config->bank_b_backend <= PWM_DRIVER_CONFIG_BANK_BACKEND_SW) &&
           (config->bank_c_backend == PWM_DRIVER_CONFIG_BANK_BACKEND_SW) &&
           (config->bank_a_backend != PWM_DRIVER_CONFIG_BANK_BACKEND_PIO) &&
           (config->bank_b_backend != PWM_DRIVER_CONFIG_BANK_BACKEND_HW) &&
           (config->bank_a_role <= PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR) &&
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

    candidate.bank_a_backend = (pwm_driver_config_bank_backend_t)record->backends[PWM_DRIVER_CONFIG_BANK_A];
    candidate.bank_b_backend = (pwm_driver_config_bank_backend_t)record->backends[PWM_DRIVER_CONFIG_BANK_B];
    candidate.bank_c_backend = (pwm_driver_config_bank_backend_t)record->backends[PWM_DRIVER_CONFIG_BANK_C];
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
    record.backends[PWM_DRIVER_CONFIG_BANK_A] = (uint8_t)config->bank_a_backend;
    record.backends[PWM_DRIVER_CONFIG_BANK_B] = (uint8_t)config->bank_b_backend;
    record.backends[PWM_DRIVER_CONFIG_BANK_C] = (uint8_t)config->bank_c_backend;
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
    multicore_lockout_start_blocking();
    flash_range_erase(slot_offset, FLASH_SECTOR_SIZE);
    flash_range_program(slot_offset, sector, FLASH_SECTOR_SIZE);
    multicore_lockout_end_blocking();
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

bool pwm_driver_config_set_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_backend_t backend,
                                pwm_driver_config_bank_role_t role) {
    pwm_driver_config_t candidate = pwm_driver_config_target;
    if ((bank >= PWM_DRIVER_CONFIG_BANK_COUNT) || (backend > PWM_DRIVER_CONFIG_BANK_BACKEND_SW) ||
        (role > PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR))
        return false;
    switch (bank) {
    case PWM_DRIVER_CONFIG_BANK_A:
        candidate.bank_a_backend = backend;
        candidate.bank_a_role = role;
        break;
    case PWM_DRIVER_CONFIG_BANK_B:
        candidate.bank_b_backend = backend;
        candidate.bank_b_role = role;
        break;
    case PWM_DRIVER_CONFIG_BANK_C:
        candidate.bank_c_backend = backend;
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

/**
 * @brief Runtime logical channel table.
 *
 * All channels start `DISABLED` at boot. Each bank's 8 channels are filled in
 * once by `pwm_driver_configure_table()` before Core 1 starts.
 */
static pwm_driver_config_channel_t pwm_driver_config_channels[PWM_DRIVER_CONFIG_CHANNEL_COUNT] = {
    [0 ... PWM_DRIVER_CONFIG_CHANNEL_COUNT - 1] = {.direction = PWM_DRIVER_CONFIG_DIRECTION_DISABLED},
};
static bool pwm_driver_config_table_configured = false;

const pwm_driver_config_channel_t *pwm_driver_config_get_channel(uint channel) {
    if (channel >= PWM_DRIVER_CONFIG_CHANNEL_COUNT) {
        return NULL;
    }

    return &pwm_driver_config_channels[channel];
}

bool pwm_driver_config_get_gpio(pwm_driver_config_backend_t backend, uint backend_channel, uint *gpio_out) {
    if (gpio_out == NULL) {
        return false;
    }

    for (uint channel = 0u; channel < PWM_DRIVER_CONFIG_CHANNEL_COUNT; ++channel) {
        const pwm_driver_config_channel_t *profile = &pwm_driver_config_channels[channel];
        if (profile->direction != PWM_DRIVER_CONFIG_DIRECTION_DISABLED && profile->backend == backend &&
            profile->backend_channel == backend_channel) {
            *gpio_out = profile->gpio;
            return true;
        }
    }

    return false;
}

bool pwm_driver_config_get_logical_channel(pwm_driver_config_backend_t backend, uint backend_channel,
                                           uint *channel_out) {
    if (channel_out == NULL) {
        return false;
    }

    for (uint channel = 0u; channel < PWM_DRIVER_CONFIG_CHANNEL_COUNT; ++channel) {
        const pwm_driver_config_channel_t *profile = &pwm_driver_config_channels[channel];
        if (profile->direction != PWM_DRIVER_CONFIG_DIRECTION_DISABLED && profile->backend == backend &&
            profile->backend_channel == backend_channel) {
            *channel_out = channel;
            return true;
        }
    }

    return false;
}

uint pwm_driver_config_backend_channel_count(pwm_driver_config_backend_t backend) {
    uint count = 0u;

    for (uint channel = 0u; channel < PWM_DRIVER_CONFIG_CHANNEL_COUNT; ++channel) {
        count += (pwm_driver_config_channels[channel].direction != PWM_DRIVER_CONFIG_DIRECTION_DISABLED &&
                  pwm_driver_config_channels[channel].backend == backend)
                     ? 1u
                     : 0u;
    }

    return count;
}

bool pwm_driver_config_validate_table(const pwm_driver_config_channel_t *channels, uint count) {
    if (channels == NULL || count != PWM_DRIVER_CONFIG_CHANNEL_COUNT) {
        return false;
    }

    for (uint channel = 0u; channel < count; ++channel) {
        const pwm_driver_config_channel_t *profile = &channels[channel];

        if (profile->backend > PWM_DRIVER_CONFIG_BACKEND_SW_MONITOR) {
            return false;
        }
        if (profile->direction == PWM_DRIVER_CONFIG_DIRECTION_DISABLED) {
            if (profile->capabilities != 0u) {
                return false;
            }
            continue;
        }
        if (profile->gpio >= PWM_DRIVER_CONFIG_GPIO_COUNT ||
            (profile->gpio < 32u && (PWM_DRIVER_CONFIG_RESERVED_GPIO_MASK & (1u << profile->gpio)) != 0u)) {
            return false;
        }
        if ((profile->gpio == 23u || profile->gpio == 24u) &&
            (profile->backend != PWM_DRIVER_CONFIG_BACKEND_SW_GENERATOR &&
             profile->backend != PWM_DRIVER_CONFIG_BACKEND_SW_MONITOR)) {
            return false;
        }
        if (profile->max_frequency_hz < profile->min_frequency_hz || profile->accuracy_ppm == 0u) {
            return false;
        }
        if ((profile->direction == PWM_DRIVER_CONFIG_DIRECTION_OUTPUT) &&
            (profile->backend >= PWM_DRIVER_CONFIG_BACKEND_HW_MONITOR ||
             (profile->capabilities & (PWM_DRIVER_CONFIG_CAP_READ | PWM_DRIVER_CONFIG_CAP_SET)) !=
                 (PWM_DRIVER_CONFIG_CAP_READ | PWM_DRIVER_CONFIG_CAP_SET))) {
            return false;
        }
        if ((profile->direction == PWM_DRIVER_CONFIG_DIRECTION_INPUT) &&
            (profile->backend < PWM_DRIVER_CONFIG_BACKEND_HW_MONITOR ||
             (profile->capabilities & PWM_DRIVER_CONFIG_CAP_SET) != 0u ||
             (profile->capabilities & PWM_DRIVER_CONFIG_CAP_READ) == 0u)) {
            return false;
        }
        if ((profile->backend == PWM_DRIVER_CONFIG_BACKEND_HW_GENERATOR ||
             profile->backend == PWM_DRIVER_CONFIG_BACKEND_HW_MONITOR) &&
            ((profile->backend_channel >= 8u) ||
             (PWM_DRIVER_CONFIG_REQUIRE_HW_CHANNEL_B && pwm_gpio_to_channel(profile->gpio) != PWM_CHAN_B))) {
            return false;
        }
        if ((profile->backend == PWM_DRIVER_CONFIG_BACKEND_PIO_GENERATOR ||
             profile->backend == PWM_DRIVER_CONFIG_BACKEND_PIO_MONITOR) &&
            profile->backend_channel >= 8u) {
            return false;
        }

        for (uint other = channel + 1u; other < count; ++other) {
            const pwm_driver_config_channel_t *candidate = &channels[other];
            if (candidate->direction != PWM_DRIVER_CONFIG_DIRECTION_DISABLED && candidate->gpio == profile->gpio) {
                return false;
            }
            if (candidate->direction != PWM_DRIVER_CONFIG_DIRECTION_DISABLED &&
                candidate->backend == profile->backend && candidate->backend_channel == profile->backend_channel) {
                return false;
            }
        }
    }

    return true;
}

bool pwm_driver_config_validate(void) {
    return pwm_driver_config_validate_table(pwm_driver_config_channels, PWM_DRIVER_CONFIG_CHANNEL_COUNT);
}

bool pwm_driver_config_frequency_supported(uint channel, uint32_t frequency_hz) {
    const pwm_driver_config_channel_t *profile = pwm_driver_config_get_channel(channel);

    if (profile == NULL || frequency_hz == 0u) {
        return profile != NULL && frequency_hz == 0u;
    }

    return frequency_hz >= profile->min_frequency_hz && frequency_hz <= profile->max_frequency_hz;
}

bool pwm_driver_config_is_monitor(void) {
    return pwm_driver_config_backend_channel_count(PWM_DRIVER_CONFIG_BACKEND_HW_MONITOR) != 0u ||
           pwm_driver_config_backend_channel_count(PWM_DRIVER_CONFIG_BACKEND_PIO_MONITOR) != 0u ||
           pwm_driver_config_backend_channel_count(PWM_DRIVER_CONFIG_BACKEND_SW_MONITOR) != 0u;
}

const char *pwm_driver_config_backend_name(pwm_driver_config_backend_t backend) {
    switch (backend) {
    case PWM_DRIVER_CONFIG_BACKEND_HW_GENERATOR:
        return "HW";
    case PWM_DRIVER_CONFIG_BACKEND_PIO_GENERATOR:
        return "PIO";
    case PWM_DRIVER_CONFIG_BACKEND_SW_GENERATOR:
        return "SW";
    case PWM_DRIVER_CONFIG_BACKEND_HW_MONITOR:
        return "HW-MON";
    case PWM_DRIVER_CONFIG_BACKEND_PIO_MONITOR:
        return "PIO-MON";
    case PWM_DRIVER_CONFIG_BACKEND_SW_MONITOR:
        return "SW-MON";
    default:
        return "?";
    }
}

/** @brief Fixed GPIO assignment for each bank's 8 logical channels; see docs/pinout.md. */
static const uint pwm_driver_config_bank_gpio[PWM_DRIVER_CONFIG_BANK_COUNT][PWM_DRIVER_CONFIG_BANK_SIZE] = {
    [PWM_DRIVER_CONFIG_BANK_A] = {1u, 3u, 5u, 7u, 9u, 11u, 13u, 15u},
    [PWM_DRIVER_CONFIG_BANK_B] = {0u, 2u, 4u, 6u, 8u, 10u, 12u, 14u},
    [PWM_DRIVER_CONFIG_BANK_C] = {16u, 17u, 18u, 19u, 20u, 21u, 22u, 28u},
};

/** @brief Fill one bank's 8 channel entries for the resolved backend and role. */
static void pwm_driver_config_fill_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_backend_t backend,
                                        pwm_driver_config_bank_role_t role) {
    uint base = (uint)bank * PWM_DRIVER_CONFIG_BANK_SIZE;
    bool generator = (role == PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR);

    for (uint i = 0u; i < PWM_DRIVER_CONFIG_BANK_SIZE; ++i) {
        uint gpio = pwm_driver_config_bank_gpio[bank][i];

        switch (backend) {
        case PWM_DRIVER_CONFIG_BANK_BACKEND_HW:
            pwm_driver_config_channels[base + i] =
                generator ? (pwm_driver_config_channel_t)PWM_DRIVER_CONFIG_HW_GENERATOR_CHANNEL(gpio, i)
                          : (pwm_driver_config_channel_t)PWM_DRIVER_CONFIG_HW_MONITOR_CHANNEL(gpio, i);
            break;
        case PWM_DRIVER_CONFIG_BANK_BACKEND_PIO:
            pwm_driver_config_channels[base + i] =
                generator ? (pwm_driver_config_channel_t)PWM_DRIVER_CONFIG_PIO_GENERATOR_CHANNEL(gpio, i)
                          : (pwm_driver_config_channel_t)PWM_DRIVER_CONFIG_PIO_MONITOR_CHANNEL(gpio, i);
            break;
        case PWM_DRIVER_CONFIG_BANK_BACKEND_SW:
        default:
            pwm_driver_config_channels[base + i] =
                generator ? (pwm_driver_config_channel_t)PWM_DRIVER_CONFIG_SW_GENERATOR_CHANNEL(gpio, i)
                          : (pwm_driver_config_channel_t)PWM_DRIVER_CONFIG_SW_MONITOR_CHANNEL(gpio, i);
            break;
        }
    }
}

bool pwm_driver_configure_table(const pwm_driver_config_bank_backend_t backends[PWM_DRIVER_CONFIG_BANK_COUNT],
                                const pwm_driver_config_bank_role_t roles[PWM_DRIVER_CONFIG_BANK_COUNT]) {
    if ((backends == NULL) || (roles == NULL) || pwm_driver_config_table_configured) {
        return false;
    }

    for (pwm_driver_config_bank_t bank = PWM_DRIVER_CONFIG_BANK_A; bank < PWM_DRIVER_CONFIG_BANK_COUNT; ++bank) {
        if ((roles[bank] != PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR) &&
            (roles[bank] != PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR)) {
            return false;
        }
        if ((backends[bank] > PWM_DRIVER_CONFIG_BANK_BACKEND_SW) ||
            (bank == PWM_DRIVER_CONFIG_BANK_A && backends[bank] == PWM_DRIVER_CONFIG_BANK_BACKEND_PIO) ||
            (bank == PWM_DRIVER_CONFIG_BANK_B && backends[bank] == PWM_DRIVER_CONFIG_BANK_BACKEND_HW) ||
            (bank == PWM_DRIVER_CONFIG_BANK_C && backends[bank] != PWM_DRIVER_CONFIG_BANK_BACKEND_SW)) {
            return false;
        }
    }

    for (pwm_driver_config_bank_t bank = PWM_DRIVER_CONFIG_BANK_A; bank < PWM_DRIVER_CONFIG_BANK_COUNT; ++bank) {
        pwm_driver_config_fill_bank(bank, backends[bank], roles[bank]);
    }

    if (!pwm_driver_config_validate()) {
        return false;
    }
    pwm_driver_config_table_configured = true;
    return true;
}
