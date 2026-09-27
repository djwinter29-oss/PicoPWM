#include "i2c/i2c_control_map.h"

#include "board/led.h"
#include "board/system.h"
#include "device_api/device_api.h"

#include <assert.h>
#include <string.h>

static pwm_profile_bank_state_t bank_states[PWM_PROFILE_BANK_COUNT] = {
    PWM_PROFILE_BANK_STATE_GENERATOR,
    PWM_PROFILE_BANK_STATE_UNLOCKED,
    PWM_PROFILE_BANK_STATE_MONITOR,
};

const char *device_api_device_name(void) {
    return "PicoPWM";
}

const char *device_api_firmware_version(void) {
    return "test";
}

pwm_driver_result_t device_api_lock_bank(pwm_profile_bank_t bank, pwm_profile_bank_role_t role) {
    (void)role;
    if (bank >= PWM_PROFILE_BANK_COUNT) {
        return PWM_DRIVER_RESULT_INVALID;
    }
    bank_states[bank] = PWM_PROFILE_BANK_STATE_GENERATOR;
    return PWM_DRIVER_RESULT_OK;
}

pwm_profile_bank_state_t device_api_get_bank_state(pwm_profile_bank_t bank) {
    return bank < PWM_PROFILE_BANK_COUNT ? bank_states[bank] : PWM_PROFILE_BANK_STATE_UNLOCKED;
}

uint8_t device_api_channel_count(void) {
    return PWM_DRIVER_CHANNEL_COUNT;
}

bool device_api_get_channel(uint channel, pwm_driver_state_t *state) {
    if ((channel >= PWM_DRIVER_CHANNEL_COUNT) || (state == NULL) ||
        (channel >= 8u && bank_states[PWM_PROFILE_BANK_PIO] == PWM_PROFILE_BANK_STATE_UNLOCKED)) {
        return false;
    }

    *state = (pwm_driver_state_t){1000u, 50u, 7u};
    return true;
}

pwm_driver_result_t device_api_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    (void)channel;
    (void)freq_hz;
    (void)duty;
    return PWM_DRIVER_RESULT_OK;
}

pwm_driver_result_t device_api_restore_defaults(void) {
    return PWM_DRIVER_RESULT_OK;
}

void led_set(bool enabled) {
    (void)enabled;
}

void system_reboot(void) {
}

static void test_expected_lengths(void) {
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_INFO) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_BANK_LOCK) == 3u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_LED) == 2u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_SET_BASE) == 6u);
    assert(i2c_control_map_expected_write_length(0xffu) == 0u);
}

static void test_read_responses(void) {
    uint8_t response[64] = {0u};
    uint8_t response_len = 0u;

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_INFO, 0u, response, &response_len));
    assert(response_len == 8u);
    assert(strcmp((const char *)response, "PicoPWM") == 0);

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_BANK_STATE, 0u, response, &response_len));
    assert(response_len == 3u);
    assert(response[0] == PWM_PROFILE_BANK_STATE_GENERATOR);
    assert(response[1] == PWM_PROFILE_BANK_STATE_UNLOCKED);
    assert(response[2] == PWM_PROFILE_BANK_STATE_MONITOR);

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CH_BASE, 0u, response, &response_len));
    assert(response_len == 9u);
    assert(response[0] == 0xe8u);
    assert(response[1] == 0x03u);
    assert(response[4] == 50u);
    assert(response[5] == 7u);

    assert(i2c_control_map_read_register((uint8_t)(I2C_CONTROL_MAP_REG_CH_BASE + 8u), 0u, response, &response_len));
    assert(response_len == 1u);
    assert(response[0] == PWM_DRIVER_RESULT_UNAVAILABLE);
}

static void test_write_validation(void) {
    const uint8_t bank_lock[] = {1u, PWM_PROFILE_BANK_ROLE_MONITOR};
    const uint8_t channel_payload[] = {0u, 0u, 0u, 0u, 50u};

    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_BANK_LOCK, bank_lock, 2u) == PWM_DRIVER_RESULT_OK);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_BANK_LOCK, NULL, 2u) == PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_BANK_LOCK, bank_lock, 1u) == PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_SET_BASE, channel_payload, 4u) == PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_SET_BASE, channel_payload, 5u) == PWM_DRIVER_RESULT_OK);
}

int main(void) {
    test_expected_lengths();
    test_read_responses();
    test_write_validation();
    return 0;
}
