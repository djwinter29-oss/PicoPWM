#include "app/i2c/i2c_control_map.h"

#include "platform/led.h"
#include "platform/system.h"
#include "platform/boot_config.h"
#include "device.h"

#include <assert.h>
#include <string.h>

static bool set_bank_ok = true;
static bool save_ok = true;
static bool address_ok = true;
static bool rebooted;
static int led_calls;

const char *device_name(void) {
    return "PicoPWM";
}

const char *device_firmware_version(void) {
    return "test";
}

pwm_role_t boot_config_get_bank(pwm_bank_t bank) {
    (void)bank;
    return PWM_ROLE_GENERATOR;
}

uint8_t boot_config_get_i2c_address(void) {
    return 0u;
}

pwm_role_t device_bank_role(pwm_bank_t bank) {
    return boot_config_get_bank(bank);
}

uint8_t device_i2c_address(void) {
    return boot_config_get_i2c_address();
}

bool boot_config_set_bank(pwm_bank_t bank, pwm_role_t role) {
    (void)bank;
    (void)role;
    return set_bank_ok;
}

bool boot_config_save(void) {
    return save_ok;
}

bool boot_config_set_i2c_address(uint8_t address) {
    (void)address;
    return address_ok;
}

uint8_t device_channel_count(void) {
    return PWM_DRIVER_CHANNEL_COUNT;
}

bool device_get_channel(uint channel, pwm_state_t *state) {
    if ((channel >= PWM_DRIVER_CHANNEL_COUNT) || (state == NULL) || (channel >= 8u)) {
        return false;
    }

    *state = (pwm_state_t){1000u, 50u};
    return true;
}

pwm_driver_result_t device_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    (void)channel;
    (void)freq_hz;
    (void)duty;
    return PWM_DRIVER_RESULT_OK;
}

pwm_driver_result_t device_restore_defaults(void) {
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
}

static void test_read_responses(void) {
    uint8_t response[64] = {0u};
    uint8_t response_len = 0u;

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_INFO, 0u, response, sizeof(response), &response_len));
    assert(response_len == 8u);
    assert(strcmp((const char *)response, "PicoPWM") == 0);

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CONFIG, 0u, response, sizeof(response), &response_len));
    assert(response_len == 14u);
    assert(response[0] == 0u);
    assert(response[2] == 1u);
    assert(response[4] == 2u);

    assert(i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CH_BASE, 0u, response, sizeof(response), &response_len));
    assert(response_len == 5u);
    assert(response[0] == 0xe8u);
    assert(response[1] == 0x03u);
    assert(response[4] == 50u);

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
    const uint8_t bank_payload[] = {0u, PWM_ROLE_MONITOR};
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
    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CH_BASE, 0u, response, 4u, &response_len));

    assert(!i2c_control_map_read_register(I2C_CONTROL_MAP_REG_CONFIG, 0u, response, 13u, &response_len));

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
    assert(!i2c_control_map_read_register(0xffu, 0u, response, 0u, &response_len));

    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_VERSION) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_CHANNEL_COUNT) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_CONFIG) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_STOP_ALL) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_REBOOT) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_CONFIG_SAVE) == 1u);
    assert(i2c_control_map_expected_write_length(I2C_CONTROL_MAP_REG_CONFIG_SET) == 3u);
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

    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_SET, bank_payload, 1u) ==
           PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_SET, NULL, 2u) == PWM_DRIVER_RESULT_INVALID);
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_SET, bank_payload, 2u) == PWM_DRIVER_RESULT_OK);
    set_bank_ok = false;
    assert(i2c_control_map_execute_write(I2C_CONTROL_MAP_REG_CONFIG_SET, bank_payload, 2u) ==
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
