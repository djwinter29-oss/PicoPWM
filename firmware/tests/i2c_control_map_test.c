#include "i2c/i2c_control_map.h"

#include "board/led.h"
#include "board/system.h"
#include "device_api/device_api.h"

#include <assert.h>
#include <string.h>

static bool config_read_ok = true;
static bool set_bank_ok = true;
static bool save_ok = true;
static bool address_ok = true;
static bool rebooted;
static int led_calls;

const char *device_api_device_name(void) {
    return "PicoPWM";
}

const char *device_api_firmware_version(void) {
    return "test";
}

bool device_api_config_get_running(pwm_driver_config_t *config) {
    if ((config == NULL) || !config_read_ok)
        return false;
    *config = (pwm_driver_config_t){
        .bank_a_backend = PWM_DRIVER_CONFIG_BANK_BACKEND_HW,
        .bank_b_backend = PWM_DRIVER_CONFIG_BANK_BACKEND_PIO,
        .bank_c_backend = PWM_DRIVER_CONFIG_BANK_BACKEND_SW,
        .bank_a_role = PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR,
        .bank_b_role = PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR,
        .bank_c_role = PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR,
    };
    return true;
}

bool device_api_config_get_target(pwm_driver_config_t *config) {
    return device_api_config_get_running(config);
}

bool device_api_config_set_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_backend_t backend,
                                pwm_driver_config_bank_role_t role) {
    (void)bank;
    (void)backend;
    (void)role;
    return set_bank_ok;
}

bool device_api_config_save_target(void) {
    return save_ok;
}

bool device_api_config_set_i2c_address(uint8_t address) {
    (void)address;
    return address_ok;
}

uint8_t device_api_channel_count(void) {
    return PWM_DRIVER_CHANNEL_COUNT;
}

bool device_api_get_channel(uint channel, pwm_driver_state_t *state) {
    if ((channel >= PWM_DRIVER_CHANNEL_COUNT) || (state == NULL) || (channel >= 8u)) {
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
    led_calls += enabled ? 1 : 0;
}

void system_reboot(void) {
    rebooted = true;
}

static void test_expected_lengths(void) {
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_INFO) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_LED) == 2u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_SET_BASE) == 6u);
    assert(i2c_control_map_expected_write_length(0xffu) == 0u);
    assert(i2c_control_map_is_status_select(1u, 6u));
    assert(i2c_control_map_is_status_select(1u, 2u));
    assert(!i2c_control_map_is_status_select(1u, 1u));
    assert(!i2c_control_map_is_status_select(6u, 6u));
    assert(!i2c_control_map_is_status_select(0u, 6u));
}

static void test_read_responses(void) {
    uint8_t response[64] = {0u};
    uint8_t response_len = 0u;

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_INFO, 0u, response, sizeof(response), &response_len));
    assert(response_len == 8u);
    assert(strcmp((const char *)response, "PicoPWM") == 0);

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CONFIG, 0u, response, sizeof(response), &response_len));
    assert(response_len == 14u);
    assert(response[0] == PWM_DRIVER_CONFIG_BANK_BACKEND_HW);
    assert(response[2] == PWM_DRIVER_CONFIG_BANK_BACKEND_PIO);
    assert(response[4] == PWM_DRIVER_CONFIG_BANK_BACKEND_SW);

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CH_BASE, 0u, response, sizeof(response), &response_len));
    assert(response_len == 9u);
    assert(response[0] == 0xe8u);
    assert(response[1] == 0x03u);
    assert(response[4] == 50u);
    assert(response[5] == 7u);

    assert(i2c_control_map_read_register((uint8_t)(I2C_CONTROL_MAP_REG_CH_BASE + 8u), 0u, response, sizeof(response),
                                         &response_len));
    assert(response_len == 1u);
    assert(response[0] == PWM_DRIVER_RESULT_UNAVAILABLE);
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CONFIG, 0u, response, 13u, &response_len));
}

static void test_write_validation(void) {
    const uint8_t channel_payload[] = {0u, 0u, 0u, 0u, 50u};
    const uint8_t address_payload[] = {0x40u};

    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_SET_BASE, channel_payload, 4u) ==
           PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_SET_BASE, channel_payload, 5u) == PWM_DRIVER_RESULT_OK);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_ADDRESS, address_payload, 0u) ==
           PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_ADDRESS, address_payload, 1u) ==
           PWM_DRIVER_RESULT_OK);
}

static void test_remaining_registers(void) {
    uint8_t response[64];
    uint8_t response_len = 0u;
    const uint8_t led_on[] = {1u};
    const uint8_t led_off[] = {0u};
    const uint8_t led_bad[] = {2u};
    const uint8_t bank_payload[] = {0u, PWM_DRIVER_CONFIG_BANK_BACKEND_HW, PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR};
    const uint8_t address_payload[] = {0x41u};
    const uint8_t address_low[] = {0x01u};
    uint8_t extra = 1u;

    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_INFO, 0u, NULL, sizeof(response), &response_len));
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_INFO, 0u, response, sizeof(response), NULL));
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_INFO, 0u, response, 1u, &response_len));
    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_VERSION, 0u, response, sizeof(response), &response_len));
    assert(strcmp((const char *)response, "test") == 0);
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_VERSION, 0u, response, 1u, &response_len));

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CHANNEL_COUNT, 0u, response, sizeof(response),
                                         &response_len));
    assert(response_len == 1u);
    assert(response[0] == PWM_DRIVER_CHANNEL_COUNT);
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CHANNEL_COUNT, 0u, response, 0u, &response_len));
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CH_BASE, 0u, response, 8u, &response_len));

    config_read_ok = false;
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CONFIG, 0u, response, sizeof(response), &response_len));
    config_read_ok = true;

    assert(i2c_control_map_is_write_register(I2C_CONTROL_MAP_REG_STOP_ALL));
    assert(i2c_control_map_is_write_register(I2C_CONTROL_MAP_REG_LED));
    assert(i2c_control_map_is_write_register(I2C_CONTROL_MAP_REG_CONFIG_SET));
    assert(!i2c_control_map_is_write_register(I2C_CONTROL_MAP_REG_INFO));
    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_STOP_ALL, PWM_DRIVER_RESULT_BUSY, response,
                                         sizeof(response), &response_len));
    assert(response_len == 1u);
    assert(response[0] == PWM_DRIVER_RESULT_BUSY);
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_STOP_ALL, 0u, response, 0u, &response_len));
    assert(!i2c_control_map_read_register(0xffu, 0u, response, sizeof(response), &response_len));
    assert(response[0] == PWM_DRIVER_RESULT_INVALID);
    assert(!i2c_control_map_read_register(0xffu, 0u, response, 0u, &response_len));

    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_VERSION) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_CHANNEL_COUNT) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_CONFIG) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_STOP_ALL) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_REBOOT) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_CONFIG_SAVE) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_CONFIG_SET) == 4u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_CONFIG_ADDRESS) == 2u);

    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_STOP_ALL, &extra, 1u) == PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_STOP_ALL, NULL, 0u) == PWM_DRIVER_RESULT_OK);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_LED, NULL, 1u) == PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_LED, led_bad, 1u) == PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_LED, led_off, 1u) == PWM_DRIVER_RESULT_OK);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_LED, led_on, 1u) == PWM_DRIVER_RESULT_OK);
    assert(led_calls == 1);

    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_REBOOT, &extra, 1u) == PWM_DRIVER_RESULT_INVALID);
    assert(!rebooted);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_REBOOT, NULL, 0u) == PWM_DRIVER_RESULT_OK);
    assert(rebooted);

    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_SET, bank_payload, 2u) ==
           PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_SET, NULL, 3u) == PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_SET, bank_payload, 3u) == PWM_DRIVER_RESULT_OK);
    set_bank_ok = false;
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_SET, bank_payload, 3u) ==
           PWM_DRIVER_RESULT_INVALID);
    set_bank_ok = true;

    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_ADDRESS, NULL, 1u) == PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_ADDRESS, address_low, 1u) ==
           PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_ADDRESS, address_payload, 1u) ==
           PWM_DRIVER_RESULT_OK);
    address_ok = false;
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_ADDRESS, address_payload, 1u) ==
           PWM_DRIVER_RESULT_INVALID);
    address_ok = true;

    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_SAVE, &extra, 1u) == PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_SAVE, NULL, 0u) == PWM_DRIVER_RESULT_OK);
    save_ok = false;
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_SAVE, NULL, 0u) == PWM_DRIVER_RESULT_INVALID);
    save_ok = true;

    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_SET_BASE, NULL, 5u) == PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(0xffu, NULL, 0u) == PWM_DRIVER_RESULT_INVALID);
}

int main(void) {
    test_expected_lengths();
    test_read_responses();
    test_write_validation();
    test_remaining_registers();
    return 0;
}
