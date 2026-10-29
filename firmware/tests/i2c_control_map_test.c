#include "app/i2c/i2c_control_map.h"
#include "app/i2c/i2c_command_queue.h"

#include "control_api.h"

#include <assert.h>
#include <string.h>

static const char *firmware_version = "12.3.4";
static pwm_role_t bank_roles[PWM_BANK_COUNT] = {PWM_ROLE_GENERATOR, PWM_ROLE_MONITOR, PWM_ROLE_GENERATOR};
static pwm_state_t channel_state = {0x12345678u, 81u};
static pwm_driver_result_t set_result = PWM_DRIVER_RESULT_OK;
static pwm_driver_result_t stop_result = PWM_DRIVER_RESULT_OK;
static unsigned set_calls;
static unsigned stop_calls;
static uint set_channel;
static uint32_t set_frequency;
static uint8_t set_duty;

const char *control_api_firmware_version(void) {
    return firmware_version;
}

pwm_role_t control_api_bank_role(pwm_bank_t bank) {
    return bank_roles[bank];
}

uint8_t control_api_channel_count(void) {
    return PWM_DRIVER_CHANNEL_COUNT;
}

bool control_api_get_channel(uint channel, pwm_state_t *state) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT || state == NULL) {
        return false;
    }
    *state = channel_state;
    return true;
}

pwm_driver_result_t control_api_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    set_calls++;
    set_channel = channel;
    set_frequency = freq_hz;
    set_duty = duty;
    return set_result;
}

pwm_driver_result_t control_api_restore_defaults(void) {
    stop_calls++;
    return stop_result;
}

static void reset(void) {
    firmware_version = "12.3.4";
    set_result = PWM_DRIVER_RESULT_OK;
    stop_result = PWM_DRIVER_RESULT_OK;
    set_calls = stop_calls = 0u;
    i2c_control_map_init();
}

static void write_frame(const uint8_t *frame, uint8_t length, bool overflow) {
    i2c_control_map_ops.write_frame(frame, length, overflow);
}

static uint8_t read_response(uint8_t *response, uint8_t capacity) {
    uint8_t length = 0u;
    assert(i2c_control_map_ops.read_response(response, capacity, &length));
    return length;
}

static void test_info_register(void) {
    uint8_t response[8] = {0u};

    reset();
    assert(read_response(response, sizeof(response)) == 0u);
    const uint8_t select[] = {I2C_CONTROL_MAP_REG_INFO};
    write_frame(select, sizeof(select), false);
    assert(read_response(response, sizeof(response)) == I2C_CONTROL_MAP_INFO_SIZE);
    assert(memcmp(response, "PPWM", 4u) == 0);
    assert(response[4] == 12u);
    assert(response[5] == PWM_DRIVER_CHANNEL_COUNT);
    assert(response[6] == 0x05u);

    firmware_version = "99.0.0";
    assert(read_response(response, sizeof(response)) == I2C_CONTROL_MAP_INFO_SIZE);
    assert(response[4] == 12u);

    firmware_version = "256.0.0";
    i2c_control_map_init();
    const uint8_t invalid_info_select[] = {I2C_CONTROL_MAP_REG_INFO};
    write_frame(invalid_info_select, sizeof(invalid_info_select), false);
    assert(!i2c_control_map_ops.read_response(response, sizeof(response), &(uint8_t){0u}));
}

static void test_channel_read_encoding_and_unknown_register(void) {
    uint8_t response[5] = {0u};
    const uint8_t select[] = {I2C_CONTROL_MAP_REG_CH_BASE};

    reset();
    write_frame(select, sizeof(select), false);
    assert(read_response(response, sizeof(response)) == sizeof(response));
    assert(response[0] == 0x78u && response[1] == 0x56u && response[2] == 0x34u && response[3] == 0x12u);
    assert(response[4] == 81u);

    const uint8_t unknown[] = {0xffu};
    write_frame(unknown, sizeof(unknown), false);
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_INVALID);
}

static void test_malformed_read_only_selector_does_not_replace_selection(void) {
    uint8_t response[8] = {0u};
    const uint8_t channel_select[] = {I2C_CONTROL_MAP_REG_CH_BASE};
    const uint8_t malformed_info[] = {I2C_CONTROL_MAP_REG_INFO, 0x55u};

    reset();
    write_frame(channel_select, sizeof(channel_select), false);
    write_frame(malformed_info, sizeof(malformed_info), false);
    assert(read_response(response, sizeof(response)) == 5u);
    assert(response[0] == 0x78u && response[1] == 0x56u && response[2] == 0x34u && response[3] == 0x12u);
    assert(response[4] == 81u);
}

static void test_channel_command_status_validation_and_dispatch(void) {
    uint8_t response[1] = {0u};
    const uint8_t select[] = {I2C_CONTROL_MAP_REG_SET_BASE};
    const uint8_t command[] = {I2C_CONTROL_MAP_REG_SET_BASE, 0x78u, 0x56u, 0x34u, 0x12u, 50u};
    const uint8_t short_command[] = {I2C_CONTROL_MAP_REG_SET_BASE, 0x78u};
    const uint8_t overlong[] = {I2C_CONTROL_MAP_REG_SET_BASE, 0x78u, 0x56u, 0x34u, 0x12u, 50u, 0u};

    reset();
    write_frame(select, sizeof(select), false);
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_NOT_RUN);

    write_frame(short_command, sizeof(short_command), false);
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_INVALID);
    write_frame(overlong, sizeof(overlong), false);
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_INVALID);

    write_frame(command, sizeof(command), false);
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_BUSY);
    assert(set_calls == 0u);
    i2c_control_map_poll();
    assert(set_calls == 1u && set_channel == 0u);
    assert(set_frequency == 0x12345678u && set_duty == 50u);
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_OK);
}

static void test_malformed_overflow_and_status_shortcut(void) {
    uint8_t response[1];
    const uint8_t select[] = {I2C_CONTROL_MAP_REG_SET_BASE};
    const uint8_t frame[] = {I2C_CONTROL_MAP_REG_SET_BASE, 1u, 2u, 3u, 4u, 5u};

    reset();
    write_frame(select, sizeof(select), true);
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_INVALID);

    write_frame(frame, sizeof(frame), true);
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_INVALID);
    assert(set_calls == 0u);

    reset();
    write_frame(select, sizeof(select), false);
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_NOT_RUN);
    i2c_control_map_poll();
    assert(set_calls == 0u);
}

static void test_stop_all_and_queue_full(void) {
    uint8_t response[1];
    const uint8_t stop[] = {I2C_CONTROL_MAP_REG_STOP_ALL};

    reset();
    for (unsigned index = 0u; index < I2C_COMMAND_QUEUE_DEPTH; ++index) {
        write_frame(stop, sizeof(stop), false);
    }
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_BUSY);
    write_frame(stop, sizeof(stop), false);
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_UNAVAILABLE);
    i2c_control_map_poll();
    assert(stop_calls == 1u);
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_UNAVAILABLE);
}

static void test_stale_command_completion_does_not_replace_new_attempt(void) {
    uint8_t response[1];
    const uint8_t command[] = {I2C_CONTROL_MAP_REG_SET_BASE, 1u, 2u, 3u, 4u, 5u};

    reset();
    write_frame(command, sizeof(command), false);
    write_frame(command, sizeof(command), false);
    set_result = PWM_DRIVER_RESULT_APPLY_FAILED;
    i2c_control_map_poll();
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_BUSY);
    set_result = PWM_DRIVER_RESULT_OK;
    i2c_control_map_poll();
    assert(read_response(response, sizeof(response)) == 1u);
    assert(response[0] == I2C_STATUS_OK);
}

int main(void) {
    test_info_register();
    test_channel_read_encoding_and_unknown_register();
    test_malformed_read_only_selector_does_not_replace_selection();
    test_channel_command_status_validation_and_dispatch();
    test_malformed_overflow_and_status_shortcut();
    test_stop_all_and_queue_full();
    test_stale_command_completion_does_not_replace_new_attempt();
    return 0;
}
