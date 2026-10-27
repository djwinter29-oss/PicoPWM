/**
 * @file i2c_control_map.c
 * @brief I2C register map and protocol helpers layered on top of the shared control interface.
 *
 * This module provides the protocol logic for the I2C slave, translating register
 * numbers and payloads into control interface calls. It does not manage I2C transport;
 * see i2c_slave.c for ISR and buffering logic.
 *
 * **Register Categories**:
 * - **Read-only (0x00, 0x01, 0x02, 0x03, 0x10..0x27)**: Device info, version, target/running configuration, channel
 * state.
 * - **Write (0x04, 0x05, 0x30..0x47, 0x90, 0x91, 0x92)**: Target configuration, channel frequency/duty, stop, LED,
 * reboot.
 *
 * **Output Validation**:
 * - Device name and firmware version strings are bounds-checked to prevent I2C response
 *   buffer overflow (max 64 bytes).
 * - If a string exceeds 64 bytes, the register read is rejected with false.
 *
 * **Input Validation**:
 * - Payload lengths and register ranges are validated by i2c_slave.c before
 *   calling execute_write().
 * - This module performs additional semantic checks (e.g., channel ID, duty clamping).
 */

#include "app/i2c/i2c_control_map.h"

#include "platform/boot_config.h"
#include "device.h"
#include "platform/led.h"
#include "platform/system.h"

#include <string.h>

static bool i2c_control_map_is_channel_read(uint8_t reg) {
    return reg >= I2C_CONTROL_MAP_REG_CH_BASE &&
           reg < (uint8_t)(I2C_CONTROL_MAP_REG_CH_BASE + PWM_DRIVER_CHANNEL_COUNT);
}

static bool i2c_control_map_is_full_write(uint8_t reg) {
    return reg >= I2C_CONTROL_MAP_REG_SET_BASE &&
           reg < (uint8_t)(I2C_CONTROL_MAP_REG_SET_BASE + PWM_DRIVER_CHANNEL_COUNT);
}

bool i2c_control_map_is_write_register(uint8_t reg) {
    return i2c_control_map_is_full_write(reg) || (reg == I2C_CONTROL_MAP_REG_STOP_ALL) ||
           (reg == I2C_CONTROL_MAP_REG_LED) || (reg == I2C_CONTROL_MAP_REG_REBOOT) ||
           (reg == I2C_CONTROL_MAP_REG_CONFIG_SET) || (reg == I2C_CONTROL_MAP_REG_CONFIG_SAVE) ||
           (reg == I2C_CONTROL_MAP_REG_CONFIG_ADDRESS);
}

uint8_t i2c_control_map_expected_write_length(uint8_t reg) {
    if ((reg == I2C_CONTROL_MAP_REG_INFO) || (reg == I2C_CONTROL_MAP_REG_VERSION) ||
        (reg == I2C_CONTROL_MAP_REG_CHANNEL_COUNT) || (reg == I2C_CONTROL_MAP_REG_CONFIG) ||
        i2c_control_map_is_channel_read(reg) || (reg == I2C_CONTROL_MAP_REG_STOP_ALL) ||
        (reg == I2C_CONTROL_MAP_REG_REBOOT) || (reg == I2C_CONTROL_MAP_REG_CONFIG_SAVE)) {
        return 1u;
    }

    if (reg == I2C_CONTROL_MAP_REG_LED) {
        return 2u;
    }

    if (reg == I2C_CONTROL_MAP_REG_CONFIG_SET) {
        return 4u;
    }

    if (reg == I2C_CONTROL_MAP_REG_CONFIG_ADDRESS) {
        return 2u;
    }

    if (i2c_control_map_is_full_write(reg)) {
        return 6u;
    }

    return 0u;
}

bool i2c_control_map_read_register(uint8_t reg, uint8_t last_status, uint8_t *response, uint8_t response_capacity,
                                   uint8_t *response_len) {
    pwm_driver_state_t state = {0u, 50u};
    const char *text;
    size_t text_len;

    if ((response == NULL) || (response_len == NULL)) {
        return false;
    }

    if (reg == I2C_CONTROL_MAP_REG_INFO) {
        text = device_name();
        text_len = strlen(text) + 1u;
        if (text_len > response_capacity) {
            return false;
        }
        *response_len = (uint8_t)text_len;
        memcpy(response, text, text_len);
        return true;
    }

    if (reg == I2C_CONTROL_MAP_REG_VERSION) {
        text = device_firmware_version();
        text_len = strlen(text) + 1u;
        if (text_len > response_capacity) {
            return false;
        }
        *response_len = (uint8_t)text_len;
        memcpy(response, text, text_len);
        return true;
    }

    if (reg == I2C_CONTROL_MAP_REG_CONFIG) {
        if (response_capacity < 14u) {
            return false;
        }
        /* Per bank: fixed backend id (A=HW 0, B=PIO 1, C=SW 2), then role; running banks first, then target. */
        for (uint8_t bank = 0u; bank < PWM_BANK_COUNT; ++bank) {
            response[2u * bank] = bank;
            response[2u * bank + 1u] = (uint8_t)device_bank_role((pwm_bank_t)bank);
            response[6u + 2u * bank] = bank;
            response[6u + 2u * bank + 1u] = (uint8_t)boot_config_get_bank((pwm_bank_t)bank);
        }
        response[12] = device_i2c_address();
        response[13] = boot_config_get_i2c_address();
        *response_len = 14u;
        return true;
    }

    if (reg == I2C_CONTROL_MAP_REG_CHANNEL_COUNT) {
        if (response_capacity < 1u) {
            return false;
        }
        response[0] = device_channel_count();
        *response_len = 1u;
        return true;
    }

    if (i2c_control_map_is_channel_read(reg)) {
        if (response_capacity < 5u) {
            return false;
        }
        uint channel = (uint)(reg - I2C_CONTROL_MAP_REG_CH_BASE);
        if (!device_get_channel(channel, &state)) {
            response[0] = (uint8_t)PWM_DRIVER_RESULT_UNAVAILABLE;
            *response_len = 1u;
            return true;
        }
        memcpy(response + 0, &state.freq_hz, sizeof(uint32_t));
        memcpy(response + 4, &state.duty, sizeof(uint8_t));
        *response_len = 5u;
        return true;
    }

    if (i2c_control_map_is_write_register(reg)) {
        if (response_capacity < 1u) {
            return false;
        }
        response[0] = last_status;
        *response_len = 1u;
        return true;
    }

    if (response_capacity < 1u) {
        return false;
    }
    response[0] = (uint8_t)PWM_DRIVER_RESULT_INVALID;
    *response_len = 1u;
    return false;
}

pwm_driver_result_t i2c_control_map_execute_write(uint8_t reg, const uint8_t *payload, uint8_t payload_len) {
    uint channel;
    uint32_t value_freq;
    uint8_t value_duty;

    if (reg == I2C_CONTROL_MAP_REG_STOP_ALL) {
        if (payload_len != 0u) {
            return PWM_DRIVER_RESULT_INVALID;
        }
        return device_restore_defaults();
    }

    if (reg == I2C_CONTROL_MAP_REG_LED) {
        if ((payload == NULL) || (payload_len != 1u) || (payload[0] > 1u)) {
            return PWM_DRIVER_RESULT_INVALID;
        }

        led_set(payload[0] != 0u);
        return PWM_DRIVER_RESULT_OK;
    }

    if (reg == I2C_CONTROL_MAP_REG_REBOOT) {
        if (payload_len != 0u) {
            return PWM_DRIVER_RESULT_INVALID;
        }

        system_reboot();
        return PWM_DRIVER_RESULT_OK;
    }

    if (reg == I2C_CONTROL_MAP_REG_CONFIG_SET) {
        if ((payload == NULL) || (payload_len != 3u)) {
            return PWM_DRIVER_RESULT_INVALID;
        }
        return (payload[1] == payload[0] && boot_config_set_bank((pwm_bank_t)payload[0], (pwm_role_t)payload[2]))
                   ? PWM_DRIVER_RESULT_OK
                   : PWM_DRIVER_RESULT_INVALID;
    }

    if (reg == I2C_CONTROL_MAP_REG_CONFIG_ADDRESS) {
        if ((payload == NULL) || (payload_len != 1u) || (payload[0] < 0x08u) || (payload[0] > 0x77u)) {
            return PWM_DRIVER_RESULT_INVALID;
        }
        return boot_config_set_i2c_address(payload[0]) ? PWM_DRIVER_RESULT_OK : PWM_DRIVER_RESULT_INVALID;
    }

    if (reg == I2C_CONTROL_MAP_REG_CONFIG_SAVE) {
        if (payload_len != 0u) {
            return PWM_DRIVER_RESULT_INVALID;
        }
        return boot_config_save() ? PWM_DRIVER_RESULT_OK : PWM_DRIVER_RESULT_INVALID;
    }

    if (i2c_control_map_is_full_write(reg)) {
        if ((payload == NULL) || (payload_len != 5u)) {
            return PWM_DRIVER_RESULT_INVALID;
        }

        channel = (uint)(reg - I2C_CONTROL_MAP_REG_SET_BASE);
        memcpy(&value_freq, payload + 0, sizeof(uint32_t));
        memcpy(&value_duty, payload + 4, sizeof(uint8_t));
        return device_set_channel(channel, value_freq, value_duty);
    }

    return PWM_DRIVER_RESULT_INVALID;
}