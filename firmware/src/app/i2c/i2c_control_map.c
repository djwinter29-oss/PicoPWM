/**
 * @file i2c_control_map.c
 * @brief PWM I2C register protocol, status tracking, and command dispatch.
 *
 * The transport invokes frame/read callbacks from the I2C ISR. Frame handling
 * is bounded to validation and queue insertion; PWM commands execute in the
 * Core 0 foreground poll.
 */

#include "app/i2c/i2c_control_map.h"

#include "app/i2c/i2c_command_queue.h"
#include "control_api.h"
#include "hardware/sync.h"

#include <stdlib.h>

#define I2C_CONTROL_MAP_COMMAND_COUNT (PWM_DRIVER_CHANNEL_COUNT + 1u)

_Static_assert(I2C_COMMAND_QUEUE_PAYLOAD == I2C_CONTROL_MAP_CHANNEL_COMMAND_PAYLOAD_SIZE,
               "I2C queue payload must hold the largest protocol command");
_Static_assert((int)I2C_STATUS_OK == (int)PWM_DRIVER_RESULT_OK, "I2C OK must match the PWM result");
_Static_assert((int)I2C_STATUS_BUSY == (int)PWM_DRIVER_RESULT_BUSY, "I2C BUSY must match the PWM result");
_Static_assert((int)I2C_STATUS_INVALID == (int)PWM_DRIVER_RESULT_INVALID, "I2C INVALID must match the PWM result");
_Static_assert((int)I2C_STATUS_UNAVAILABLE == (int)PWM_DRIVER_RESULT_UNAVAILABLE,
               "I2C UNAVAILABLE must match the PWM result");
_Static_assert((int)I2C_STATUS_TIMEOUT == (int)PWM_DRIVER_RESULT_TIMEOUT, "I2C TIMEOUT must match the PWM result");
_Static_assert((int)I2C_STATUS_APPLY_FAILED == (int)PWM_DRIVER_RESULT_APPLY_FAILED,
               "I2C APPLY_FAILED must match the PWM result");

static i2c_command_queue_t command_queue;
static uint8_t command_status[I2C_CONTROL_MAP_COMMAND_COUNT];
static uint32_t command_epoch[I2C_CONTROL_MAP_COMMAND_COUNT];
static uint8_t selected_register;
static bool selected_register_valid;
static uint8_t cached_firmware_major;
static bool cached_firmware_major_valid;

static bool is_channel_read(uint8_t reg) {
    return reg >= I2C_CONTROL_MAP_REG_CH_BASE &&
           reg < (uint8_t)(I2C_CONTROL_MAP_REG_CH_BASE + PWM_DRIVER_CHANNEL_COUNT);
}

static bool is_channel_command(uint8_t reg) {
    return reg >= I2C_CONTROL_MAP_REG_SET_BASE &&
           reg < (uint8_t)(I2C_CONTROL_MAP_REG_SET_BASE + PWM_DRIVER_CHANNEL_COUNT);
}

static bool command_index(uint8_t reg, uint8_t *index) {
    if (reg == I2C_CONTROL_MAP_REG_STOP_ALL) {
        *index = 0u;
        return true;
    }
    if (is_channel_command(reg)) {
        *index = (uint8_t)(1u + reg - I2C_CONTROL_MAP_REG_SET_BASE);
        return true;
    }
    return false;
}

static uint8_t expected_write_length(uint8_t reg) {
    if (reg == I2C_CONTROL_MAP_REG_INFO || is_channel_read(reg) || reg == I2C_CONTROL_MAP_REG_STOP_ALL) {
        return 1u;
    }
    return is_channel_command(reg) ? (uint8_t)(1u + I2C_CONTROL_MAP_CHANNEL_COMMAND_PAYLOAD_SIZE) : 0u;
}

/**
 * @brief Parse the firmware major version for the REG_INFO response.
 * @param major Receives the parsed major version.
 * @return True when the version contains a uint8 major component.
 */
static bool firmware_major(uint8_t *major) {
    const char *version = control_api_firmware_version();
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

static uint8_t bank_roles(void) {
    uint8_t roles = 0u;

    for (uint8_t bank = 0u; bank < PWM_BANK_COUNT; ++bank) {
        if (control_api_bank_role((pwm_bank_t)bank) == PWM_ROLE_GENERATOR) {
            roles |= (uint8_t)(1u << bank);
        }
    }
    return roles;
}

static pwm_driver_result_t execute_command(uint8_t reg, const uint8_t *payload, uint8_t payload_len) {
    if (reg == I2C_CONTROL_MAP_REG_STOP_ALL) {
        return payload_len == 0u ? control_api_restore_defaults() : PWM_DRIVER_RESULT_INVALID;
    }
    if (is_channel_command(reg)) {
        uint32_t frequency;
        uint channel;

        if (payload == NULL || payload_len != I2C_CONTROL_MAP_CHANNEL_COMMAND_PAYLOAD_SIZE) {
            return PWM_DRIVER_RESULT_INVALID;
        }
        channel = (uint)(reg - I2C_CONTROL_MAP_REG_SET_BASE);
        frequency = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8u) |
                    ((uint32_t)payload[2] << 16u) | ((uint32_t)payload[3] << 24u);
        return control_api_set_channel(channel, frequency, payload[4]);
    }
    return PWM_DRIVER_RESULT_INVALID;
}

/**
 * @brief Interpret one complete transaction according to the PWM register map.
 * @param frame Captured transaction bytes, register first.
 * @param length Captured byte count.
 * @param overflow True when transport capture exceeded its frame capacity.
 */
static void handle_write_frame(const uint8_t *frame, uint8_t length, bool overflow) {
    uint8_t reg;
    uint8_t expected;
    uint8_t index;

    if (frame == NULL || length == 0u) {
        return;
    }

    reg = frame[0];
    expected = expected_write_length(reg);

    if (expected == 0u) {
        if (!overflow && length == 1u) {
            selected_register = reg;
            selected_register_valid = true;
        }
        return;
    }

    if (reg == I2C_CONTROL_MAP_REG_INFO || is_channel_read(reg)) {
        if (!overflow && length == 1u) {
            selected_register = reg;
            selected_register_valid = true;
        }
        return;
    }

    selected_register = reg;
    selected_register_valid = true;

    if (is_channel_command(reg) && !overflow && length == 1u) {
        return;
    }

    if (overflow || length != expected) {
        if (command_index(reg, &index)) {
            command_epoch[index]++;
            command_status[index] = (uint8_t)I2C_STATUS_INVALID;
        }
        return;
    }

    if (!command_index(reg, &index)) {
        return;
    }

    command_epoch[index]++;
    if (i2c_command_queue_push(&command_queue, reg, &frame[1], (uint8_t)(length - 1u), command_epoch[index])) {
        command_status[index] = (uint8_t)I2C_STATUS_BUSY;
    } else {
        command_status[index] = (uint8_t)I2C_STATUS_UNAVAILABLE;
    }
}

static bool build_read_response(uint8_t *response, uint8_t capacity, uint8_t *length) {
    uint8_t index;

    if (response == NULL || length == NULL) {
        return false;
    }
    *length = 0u;
    if (!selected_register_valid) {
        return true;
    }

    if (selected_register == I2C_CONTROL_MAP_REG_INFO) {
        if (capacity < I2C_CONTROL_MAP_INFO_SIZE || !cached_firmware_major_valid) {
            return false;
        }
        response[0] = 'P';
        response[1] = 'P';
        response[2] = 'W';
        response[3] = 'M';
        response[4] = cached_firmware_major;
        response[5] = control_api_channel_count();
        response[6] = bank_roles();
        *length = I2C_CONTROL_MAP_INFO_SIZE;
        return true;
    }

    if (is_channel_read(selected_register)) {
        pwm_state_t state;
        if (capacity < 5u ||
            !control_api_get_channel((uint)(selected_register - I2C_CONTROL_MAP_REG_CH_BASE), &state)) {
            return false;
        }
        response[0] = (uint8_t)state.freq_hz;
        response[1] = (uint8_t)(state.freq_hz >> 8u);
        response[2] = (uint8_t)(state.freq_hz >> 16u);
        response[3] = (uint8_t)(state.freq_hz >> 24u);
        response[4] = state.duty;
        *length = 5u;
        return true;
    }

    if (command_index(selected_register, &index)) {
        if (capacity < 1u) {
            return false;
        }
        response[0] = command_status[index];
        *length = 1u;
        return true;
    }

    if (capacity < 1u) {
        return false;
    }
    response[0] = (uint8_t)I2C_STATUS_INVALID;
    *length = 1u;
    return true;
}

/** @copydoc i2c_control_map_init */
void i2c_control_map_init(void) {
    command_queue = (i2c_command_queue_t){0};
    selected_register = 0u;
    selected_register_valid = false;
    cached_firmware_major_valid = firmware_major(&cached_firmware_major);
    for (uint8_t index = 0u; index < I2C_CONTROL_MAP_COMMAND_COUNT; ++index) {
        command_status[index] = (uint8_t)I2C_STATUS_NOT_RUN;
        command_epoch[index] = 0u;
    }
}

/** @copydoc i2c_control_map_poll */
void i2c_control_map_poll(void) {
    i2c_command_slot_t slot;
    uint8_t index;
    uint32_t irq_state = save_and_disable_interrupts();
    bool has_command = i2c_command_queue_pop(&command_queue, &slot);
    restore_interrupts(irq_state);
    if (!has_command) {
        return;
    }

    uint8_t result = (uint8_t)execute_command(slot.reg, slot.payload, slot.len);
    if (command_index(slot.reg, &index)) {
        irq_state = save_and_disable_interrupts();
        if (slot.epoch == command_epoch[index]) {
            command_status[index] = result;
        }
        restore_interrupts(irq_state);
    }
}

const i2c_slave_ops_t i2c_control_map_ops = {
    .write_frame = handle_write_frame,
    .read_response = build_read_response,
};
