#include "app/i2c/i2c_control_map.h"

#include "device.h"

#include <assert.h>
#include <string.h>

static const char *firmware_version = "12.3.4";
static pwm_role_t bank_roles[PWM_BANK_COUNT] = {PWM_ROLE_GENERATOR, PWM_ROLE_MONITOR, PWM_ROLE_GENERATOR};
static pwm_state_t channel_state = {0x12345678u, 81u};
static pwm_driver_result_t set_result = PWM_DRIVER_RESULT_OK;
static pwm_driver_result_t stop_result = PWM_DRIVER_RESULT_OK;
static uint set_channel;
static uint32_t set_frequency;
static uint8_t set_duty;

const char *device_firmware_version(void) {
    return firmware_version;
}

pwm_role_t device_bank_role(pwm_bank_t bank) {
    return bank_roles[bank];
}

uint8_t device_channel_count(void) {
    return PWM_DRIVER_CHANNEL_COUNT;
}

bool device_get_channel(uint channel, pwm_state_t *state) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT || state == NULL) {
        return false;
    }
    *state = channel_state;
    return true;
}

pwm_driver_result_t device_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    set_channel = channel;
    set_frequency = freq_hz;
    set_duty = duty;
    return set_result;
}

pwm_driver_result_t device_restore_defaults(void) {
    return stop_result;
}

static void test_register_lengths(void) {
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_INFO) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_STOP_ALL) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_CH_BASE) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_SET_BASE) == 6u);
    assert(i2c_control_map_expected_write_length(0xffu) == 0u);
    assert(!i2c_control_map_is_write_register(I2C_CONTROL_MAP_REG_INFO));
    assert(!i2c_control_map_is_write_register(I2C_CONTROL_MAP_REG_CH_BASE));
    assert(i2c_control_map_is_write_register(I2C_CONTROL_MAP_REG_STOP_ALL));
    assert(i2c_control_map_is_write_register(I2C_CONTROL_MAP_REG_SET_BASE));
}

static void test_info_register(void) {
    uint8_t response[8] = {0u};
    uint8_t response_len = 0u;

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_INFO, I2C_STATUS_NOT_RUN, response, sizeof(response),
                                         &response_len));
    assert(response_len == I2C_CONTROL_MAP_INFO_SIZE);
    assert(memcmp(response, "PPWM", 4u) == 0);
    assert(response[4] == 12u);
    assert(response[5] == PWM_DRIVER_CHANNEL_COUNT);
    assert(response[6] == 0x05u);

    firmware_version = "256.0.0";
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_INFO, I2C_STATUS_NOT_RUN, response, sizeof(response),
                                          &response_len));
    firmware_version = "invalid";
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_INFO, I2C_STATUS_NOT_RUN, response, sizeof(response),
                                          &response_len));
    firmware_version = "12.3.4";
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_INFO, I2C_STATUS_NOT_RUN, response, 6u,
                                          &response_len));
}

static void test_channel_read_is_little_endian(void) {
    uint8_t response[5] = {0u};
    uint8_t response_len = 0u;

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CH_BASE, I2C_STATUS_NOT_RUN, response, sizeof(response),
                                         &response_len));
    assert(response_len == sizeof(response));
    assert(response[0] == 0x78u && response[1] == 0x56u && response[2] == 0x34u && response[3] == 0x12u);
    assert(response[4] == 81u);
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CH_BASE, I2C_STATUS_NOT_RUN, response, 4u,
                                          &response_len));
}

static void test_command_status_responses(void) {
    uint8_t response[1];
    uint8_t response_len = 0u;

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_STOP_ALL, I2C_STATUS_NOT_RUN, response, sizeof(response),
                                         &response_len));
    assert(response_len == 1u && response[0] == I2C_STATUS_NOT_RUN);
    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_SET_BASE, I2C_STATUS_APPLY_FAILED, response,
                                         sizeof(response), &response_len));
    assert(response[0] == I2C_STATUS_APPLY_FAILED);
    assert(!i2c_control_map_read_register(0xffu, I2C_STATUS_OK, response, sizeof(response), &response_len));
}

static void test_channel_command_decode(void) {
    const uint8_t payload[] = {0x78u, 0x56u, 0x34u, 0x12u, 50u};

    assert(i2c_control_map_execute_write((uint8_t)(I2C_CONTROL_MAP_REG_SET_BASE + 9u), payload, sizeof(payload)) ==
           PWM_DRIVER_RESULT_OK);
    assert(set_channel == 9u);
    assert(set_frequency == 0x12345678u);
    assert(set_duty == 50u);
    set_result = PWM_DRIVER_RESULT_UNAVAILABLE;
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_SET_BASE, payload, sizeof(payload)) ==
           PWM_DRIVER_RESULT_UNAVAILABLE);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_SET_BASE, payload, 4u) == PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_SET_BASE, NULL, sizeof(payload)) ==
           PWM_DRIVER_RESULT_INVALID);
}

static void test_stop_all_command(void) {
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_STOP_ALL, NULL, 0u) == PWM_DRIVER_RESULT_OK);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_STOP_ALL, NULL, 1u) == PWM_DRIVER_RESULT_INVALID);
    stop_result = PWM_DRIVER_RESULT_TIMEOUT;
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_STOP_ALL, NULL, 0u) == PWM_DRIVER_RESULT_TIMEOUT);
}

int main(void) {
    test_register_lengths();
    test_info_register();
    test_channel_read_is_little_endian();
    test_command_status_responses();
    test_channel_command_decode();
    test_stop_all_command();
    return 0;
}
