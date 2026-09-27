/**
 * @file device_api.c
 * @brief Shared Core 0 device API surface for CDC and I2C transports.
 */

#include "device_api.h"

#include "config/board_config.h"
#include "pwmdriver/pwm_driver_internal.h"

const char *device_api_device_name(void) {
    return BOARD_DEVICE_NAME;
}

const char *device_api_firmware_version(void) {
    return PICO_PWM_FIRMWARE_VERSION_STR;
}

bool device_api_config_init(const pwm_driver_config_t *running, const pwm_driver_config_t *target) {
    return pwm_driver_config_init_state(running, target);
}

bool device_api_config_get_target(pwm_driver_config_t *config) {
    return pwm_driver_config_get_target(config);
}

bool device_api_config_get_running(pwm_driver_config_t *config) {
    return pwm_driver_config_get_running(config);
}

bool device_api_config_set_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_backend_t backend,
                                pwm_driver_config_bank_role_t role) {
    return pwm_driver_config_set_bank(bank, backend, role);
}

bool device_api_config_save_target(void) {
    pwm_driver_config_t target;
    return pwm_driver_config_get_target(&target) && pwm_driver_config_save_target(&target);
}

bool device_api_config_set_i2c_address(uint8_t address) {
    return pwm_driver_config_set_i2c_address(address);
}

uint8_t device_api_channel_count(void) {
    return (uint8_t)PWM_DRIVER_CHANNEL_COUNT;
}

bool device_api_get_channel(uint channel, pwm_driver_state_t *state) {
    return pwm_driver_get(channel, state);
}

pwm_driver_result_t device_api_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    return pwm_driver_set(channel, freq_hz, duty);
}

pwm_driver_result_t device_api_restore_defaults(void) {
    return pwm_driver_restore_defaults();
}
