/**
 * @file i2c_control_map.c
 * @brief I2C register map layered on top of the shared control interface.
 *
 * Translates register numbers and payloads into device calls. It does not manage I2C
 * transport; see `driver/i2c/i2c_slave.c` for the ISR, queue, and status handling.
 *
 * **Registers**:
 * - **Read-only (0x00..0x03, 0x10..0x27)**: device name, firmware version, channel count, configuration,
 *   and one 5-byte state record per channel.
 * - **Write (0x04..0x06, 0x30..0x47, 0x90..0x92)**: target configuration, target I2C address, channel
 *   frequency/duty, stop, LED, reboot. Reading a write register returns the status of its last write.
 *
 * **Validation**:
 * - The transport only accepts a write of the exact length `i2c_control_map_expected_write_length()`
 *   returns, and `i2c_control_map_execute_write()` checks the payload again.
 * - Reads run in the ISR, so every getter used here is non-blocking.
 * - A read whose response does not fit the transport buffer is rejected with `false`.
 */

#include "app/i2c/i2c_control_map.h"

#include "platform/boot_config.h"
#include "device.h"
#include "platform/led.h"
#include "platform/system.h"

#include <string.h>

_Static_assert((int)I2C_STATUS_OK == (int)PWM_DRIVER_RESULT_OK, "I2C status OK must match PWM_DRIVER_RESULT_OK");
_Static_assert((int)I2C_STATUS_QUEUED == (int)PWM_DRIVER_RESULT_BUSY, "I2C status QUEUED must match RESULT_BUSY");
_Static_assert((int)I2C_STATUS_INVALID == (int)PWM_DRIVER_RESULT_INVALID,
               "I2C status INVALID must match RESULT_INVALID");
_Static_assert((int)I2C_STATUS_REJECTED == (int)PWM_DRIVER_RESULT_UNAVAILABLE,
               "I2C status REJECTED must match RESULT_UNAVAILABLE");

/** @brief Offset of the target roles in the `REG_CONFIG` response; the running roles come first. */
#define CONFIG_TARGET_OFFSET (2u * PWM_BANK_COUNT)
/** @brief Offset of the two I2C addresses in the `REG_CONFIG` response. */
#define CONFIG_ADDRESS_OFFSET (4u * PWM_BANK_COUNT)

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

    if ((reg == I2C_CONTROL_MAP_REG_LED) || (reg == I2C_CONTROL_MAP_REG_CONFIG_ADDRESS)) {
        return 2u;
    }

    if (reg == I2C_CONTROL_MAP_REG_CONFIG_SET) {
        return 3u;
    }

    if (i2c_control_map_is_full_write(reg)) {
        return 6u;
    }

    return 0u;
}

static bool i2c_control_map_copy_text(const char *text, uint8_t *response, uint8_t response_capacity,
                                      uint8_t *response_len) {
    size_t text_len = strlen(text) + 1u;

    if (text_len > response_capacity) {
        return false;
    }
    memcpy(response, text, text_len);
    *response_len = (uint8_t)text_len;
    return true;
}

bool i2c_control_map_read_register(uint8_t reg, uint8_t last_status, uint8_t *response, uint8_t response_capacity,
                                   uint8_t *response_len) {
    if ((response == NULL) || (response_len == NULL)) {
        return false;
    }

    if (reg == I2C_CONTROL_MAP_REG_INFO) {
        return i2c_control_map_copy_text(device_name(), response, response_capacity, response_len);
    }

    if (reg == I2C_CONTROL_MAP_REG_VERSION) {
        return i2c_control_map_copy_text(device_firmware_version(), response, response_capacity, response_len);
    }

    if (reg == I2C_CONTROL_MAP_REG_CONFIG) {
        if (response_capacity < CONFIG_ADDRESS_OFFSET + 2u) {
            return false;
        }
        /* Per bank: fixed backend id (A=HW 0, B=PIO 1, C=SW 2), then role; running banks first, then target. */
        for (uint8_t bank = 0u; bank < PWM_BANK_COUNT; ++bank) {
            response[2u * bank] = bank;
            response[2u * bank + 1u] = (uint8_t)device_bank_role((pwm_bank_t)bank);
            response[CONFIG_TARGET_OFFSET + 2u * bank] = bank;
            response[CONFIG_TARGET_OFFSET + 2u * bank + 1u] = (uint8_t)boot_config_get_bank((pwm_bank_t)bank);
        }
        response[CONFIG_ADDRESS_OFFSET] = device_i2c_address();
        response[CONFIG_ADDRESS_OFFSET + 1u] = boot_config_get_i2c_address();
        *response_len = CONFIG_ADDRESS_OFFSET + 2u;
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
        pwm_state_t state;

        if (response_capacity < 5u) {
            return false;
        }
        if (!device_get_channel((uint)(reg - I2C_CONTROL_MAP_REG_CH_BASE), &state)) {
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
        if ((payload == NULL) || (payload_len != 2u)) {
            return PWM_DRIVER_RESULT_INVALID;
        }
        return boot_config_set_bank((pwm_bank_t)payload[0], (pwm_role_t)payload[1]) ? PWM_DRIVER_RESULT_OK
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

static uint8_t i2c_control_map_execute_op(uint8_t reg, const uint8_t *payload, uint8_t payload_len) {
    return (uint8_t)i2c_control_map_execute_write(reg, payload, payload_len);
}

const i2c_slave_ops_t i2c_control_map_ops = {
    .write_length = i2c_control_map_expected_write_length,
    .is_write = i2c_control_map_is_write_register,
    .read = i2c_control_map_read_register,
    .execute = i2c_control_map_execute_op,
};
