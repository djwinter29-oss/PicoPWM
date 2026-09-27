/**
 * @file device_api.c
 * @brief Shared Core 0 device API surface for CDC and I2C transports.
 */

#include "device_api.h"

#include "config/board_config.h"
#include "pwmdriver/pwm_driver_internal.h"

static pwm_driver_config_t running_config;
static pwm_driver_config_t target_config;

const char *device_api_device_name(void) {
    return BOARD_DEVICE_NAME;
}

const char *device_api_firmware_version(void) {
    return PICO_PWM_FIRMWARE_VERSION_STR;
}

void device_api_config_init(const pwm_driver_config_t *running, const pwm_driver_config_t *target) {
    if ((running != NULL) && (target != NULL)) {
        running_config = *running;
        target_config = *target;
    }
}

bool device_api_config_get_target(pwm_driver_config_t *config) {
    if (config == NULL) return false;
    *config = target_config;
    return true;
}

bool device_api_config_get_running(pwm_driver_config_t *config) {
    if (config == NULL) return false;
    *config = running_config;
    return true;
}

bool device_api_config_set_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_backend_t backend,
                                pwm_driver_config_bank_role_t role) {
    if ((bank >= PWM_DRIVER_CONFIG_BANK_COUNT) || (backend > PWM_DRIVER_CONFIG_BANK_BACKEND_SW) ||
        (role > PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR)) {
        return false;
    }

    if ((bank == PWM_DRIVER_CONFIG_BANK_A && backend == PWM_DRIVER_CONFIG_BANK_BACKEND_PIO) ||
        (bank == PWM_DRIVER_CONFIG_BANK_B && backend == PWM_DRIVER_CONFIG_BANK_BACKEND_HW) ||
        (bank == PWM_DRIVER_CONFIG_BANK_C && backend != PWM_DRIVER_CONFIG_BANK_BACKEND_SW)) {
        return false;
    }

    switch (bank) {
    case PWM_DRIVER_CONFIG_BANK_A:
        target_config.bank_a_backend = backend;
        target_config.bank_a_role = role;
        break;
    case PWM_DRIVER_CONFIG_BANK_B:
        target_config.bank_b_backend = backend;
        target_config.bank_b_role = role;
        break;
    case PWM_DRIVER_CONFIG_BANK_C:
        target_config.bank_c_backend = backend;
        target_config.bank_c_role = role;
        break;
    default:
        return false;
    }

    return true;
}

bool device_api_config_save_target(void) {
    return pwm_driver_config_save_target(&target_config);
}

bool device_api_config_set_i2c_address(uint8_t address) {
    if (address > 0x7fu) return false;
    target_config.i2c_address = address;
    return true;
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
