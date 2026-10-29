/**
 * @file i2c_control_map.c
 * @brief Translate the PWM I2C register map to the shared device control API.
 *
 * Register reads are non-blocking because they run in the I2C ISR. Writes are
 * validated and deferred by `i2c_slave.c` before reaching this module.
 */

#include "app/i2c/i2c_control_map.h"

#include "device.h"

#include <stdlib.h>

_Static_assert((int)I2C_STATUS_OK == (int)PWM_DRIVER_RESULT_OK, "I2C OK must match the PWM result");
_Static_assert((int)I2C_STATUS_BUSY == (int)PWM_DRIVER_RESULT_BUSY, "I2C BUSY must match the PWM result");
_Static_assert((int)I2C_STATUS_INVALID == (int)PWM_DRIVER_RESULT_INVALID, "I2C INVALID must match the PWM result");
_Static_assert((int)I2C_STATUS_UNAVAILABLE == (int)PWM_DRIVER_RESULT_UNAVAILABLE,
               "I2C UNAVAILABLE must match the PWM result");
_Static_assert((int)I2C_STATUS_TIMEOUT == (int)PWM_DRIVER_RESULT_TIMEOUT, "I2C TIMEOUT must match the PWM result");
_Static_assert((int)I2C_STATUS_APPLY_FAILED == (int)PWM_DRIVER_RESULT_APPLY_FAILED,
               "I2C APPLY_FAILED must match the PWM result");

static bool i2c_control_map_is_channel_read(uint8_t reg) {
    return reg >= I2C_CONTROL_MAP_REG_CH_BASE &&
           reg < (uint8_t)(I2C_CONTROL_MAP_REG_CH_BASE + PWM_DRIVER_CHANNEL_COUNT);
}

static bool i2c_control_map_is_channel_write(uint8_t reg) {
    return reg >= I2C_CONTROL_MAP_REG_SET_BASE &&
           reg < (uint8_t)(I2C_CONTROL_MAP_REG_SET_BASE + PWM_DRIVER_CHANNEL_COUNT);
}

bool i2c_control_map_is_write_register(uint8_t reg) {
    return reg == I2C_CONTROL_MAP_REG_STOP_ALL || i2c_control_map_is_channel_write(reg);
}

uint8_t i2c_control_map_expected_write_length(uint8_t reg) {
    if (reg == I2C_CONTROL_MAP_REG_INFO || i2c_control_map_is_channel_read(reg) ||
        reg == I2C_CONTROL_MAP_REG_STOP_ALL) {
        return 1u;
    }
    return i2c_control_map_is_channel_write(reg) ? 6u : 0u;
}

static bool i2c_control_map_firmware_major(uint8_t *major) {
    const char *version = device_firmware_version();
    char *end;
    unsigned long parsed;

    if (version == NULL || major == NULL) {
        return false;
    }
    parsed = strtoul(version, &end, 10);
    if (end == version || *end != '.' || parsed > UINT8_MAX) {
        return false;
    }

    *major = (uint8_t)parsed;
    return true;
}

static uint8_t i2c_control_map_bank_roles(void) {
    uint8_t roles = 0u;

    for (uint8_t bank = 0u; bank < PWM_BANK_COUNT; ++bank) {
        if (device_bank_role((pwm_bank_t)bank) == PWM_ROLE_GENERATOR) {
            roles |= (uint8_t)(1u << bank);
        }
    }
    return roles;
}

bool i2c_control_map_read_register(uint8_t reg, uint8_t last_status, uint8_t *response, uint8_t response_capacity,
                                   uint8_t *response_len) {
    if (response == NULL || response_len == NULL) {
        return false;
    }

    if (reg == I2C_CONTROL_MAP_REG_INFO) {
        uint8_t major;
        if (response_capacity < I2C_CONTROL_MAP_INFO_SIZE || !i2c_control_map_firmware_major(&major)) {
            return false;
        }
        response[0] = 'P';
        response[1] = 'P';
        response[2] = 'W';
        response[3] = 'M';
        response[4] = major;
        response[5] = device_channel_count();
        response[6] = i2c_control_map_bank_roles();
        *response_len = I2C_CONTROL_MAP_INFO_SIZE;
        return true;
    }

    if (i2c_control_map_is_channel_read(reg)) {
        pwm_state_t state;
        if (response_capacity < 5u ||
            !device_get_channel((uint)(reg - I2C_CONTROL_MAP_REG_CH_BASE), &state)) {
            return false;
        }
        response[0] = (uint8_t)state.freq_hz;
        response[1] = (uint8_t)(state.freq_hz >> 8u);
        response[2] = (uint8_t)(state.freq_hz >> 16u);
        response[3] = (uint8_t)(state.freq_hz >> 24u);
        response[4] = state.duty;
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
    if (reg == I2C_CONTROL_MAP_REG_STOP_ALL) {
        return payload_len == 0u ? device_restore_defaults() : PWM_DRIVER_RESULT_INVALID;
    }

    if (i2c_control_map_is_channel_write(reg)) {
        uint32_t frequency;
        uint8_t duty;
        uint channel = (uint)(reg - I2C_CONTROL_MAP_REG_SET_BASE);

        if (payload == NULL || payload_len != 5u) {
            return PWM_DRIVER_RESULT_INVALID;
        }
        frequency = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8u) |
                    ((uint32_t)payload[2] << 16u) | ((uint32_t)payload[3] << 24u);
        duty = payload[4];
        return device_set_channel(channel, frequency, duty);
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
