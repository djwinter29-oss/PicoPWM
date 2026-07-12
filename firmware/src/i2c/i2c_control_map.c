/**
 * @file i2c_control_map.c
 * @brief I2C register map and protocol helpers layered on top of the shared control interface.
 */

#include "i2c/i2c_control_map.h"

#include "control/control_iface.h"
#include "driver/led.h"
#include "driver/system.h"

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
    return i2c_control_map_is_full_write(reg) ||
           (reg == I2C_CONTROL_MAP_REG_STOP_ALL) ||
           (reg == I2C_CONTROL_MAP_REG_LED) ||
           (reg == I2C_CONTROL_MAP_REG_REBOOT);
}

uint8_t i2c_control_map_expected_write_length(uint8_t reg) {
    if ((reg == I2C_CONTROL_MAP_REG_INFO) ||
        (reg == I2C_CONTROL_MAP_REG_VERSION) ||
        (reg == I2C_CONTROL_MAP_REG_CHANNEL_COUNT) ||
        i2c_control_map_is_channel_read(reg) ||
        (reg == I2C_CONTROL_MAP_REG_STOP_ALL) ||
        (reg == I2C_CONTROL_MAP_REG_REBOOT)) {
        return 1u;
    }

    if (reg == I2C_CONTROL_MAP_REG_LED) {
        return 2u;
    }

    if (i2c_control_map_is_full_write(reg)) {
        return 6u;
    }

    return 0u;
}

bool i2c_control_map_read_register(uint8_t reg, uint8_t last_status, uint8_t *response, uint8_t *response_len) {
    pwm_driver_state_t state = {0u, 50u, 0u};
    const char *text;

    if ((response == NULL) || (response_len == NULL)) {
        return false;
    }

    if (reg == I2C_CONTROL_MAP_REG_INFO) {
        text = control_iface_device_name();
        *response_len = (uint8_t)(strlen(text) + 1u);
        memcpy(response, text, *response_len);
        return true;
    }

    if (reg == I2C_CONTROL_MAP_REG_VERSION) {
        text = control_iface_firmware_version();
        *response_len = (uint8_t)(strlen(text) + 1u);
        memcpy(response, text, *response_len);
        return true;
    }

    if (reg == I2C_CONTROL_MAP_REG_CHANNEL_COUNT) {
        response[0] = control_iface_channel_count();
        *response_len = 1u;
        return true;
    }

    if (i2c_control_map_is_channel_read(reg)) {
        uint channel = (uint)(reg - I2C_CONTROL_MAP_REG_CH_BASE);
        control_iface_get_channel(channel, &state);
        memcpy(response + 0, &state.freq_hz, sizeof(uint32_t));
        memcpy(response + 4, &state.duty, sizeof(uint8_t));
        memcpy(response + 5, &state.pulse_count, sizeof(uint32_t));
        *response_len = 9u;
        return true;
    }

    if (i2c_control_map_is_write_register(reg)) {
        response[0] = last_status;
        *response_len = 1u;
        return true;
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
        return control_iface_restore_defaults();
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

    if (i2c_control_map_is_full_write(reg)) {
        if ((payload == NULL) || (payload_len != 5u)) {
            return PWM_DRIVER_RESULT_INVALID;
        }

        channel = (uint)(reg - I2C_CONTROL_MAP_REG_SET_BASE);
        memcpy(&value_freq, payload + 0, sizeof(uint32_t));
        memcpy(&value_duty, payload + 4, sizeof(uint8_t));
        return control_iface_set_channel(channel, value_freq, value_duty);
    }

    return PWM_DRIVER_RESULT_INVALID;
}