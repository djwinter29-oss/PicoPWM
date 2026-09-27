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
