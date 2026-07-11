/**
 * @file control_iface.c
 * @brief Shared Core 0 control/status interface for CDC and I2C transports.
 */

#include "control/control_iface.h"

/** @brief Fixed device name reported through control/status transports. */
#define CONTROL_IFACE_DEVICE_NAME "PicoPWM"

#ifndef PICO_PWM_FIRMWARE_VERSION_STR
/** @brief Fallback firmware version when the build does not inject one. */
#define PICO_PWM_FIRMWARE_VERSION_STR "0.0.0-dev"
#endif

const char *control_iface_device_name(void) {
    return CONTROL_IFACE_DEVICE_NAME;
}

const char *control_iface_firmware_version(void) {
    return PICO_PWM_FIRMWARE_VERSION_STR;
}

uint8_t control_iface_channel_count(void) {
    return (uint8_t)PWM_DRIVER_CHANNEL_COUNT;
}

bool control_iface_get_channel(uint channel, pwm_driver_state_t *state) {
    return control_get(channel, state);
}

pwm_driver_result_t control_iface_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    return control_set(channel, freq_hz, duty);
}

pwm_driver_result_t control_iface_set_channel_freq(uint channel, uint32_t freq_hz) {
    return control_set_freq(channel, freq_hz);
}

pwm_driver_result_t control_iface_set_channel_duty(uint channel, uint8_t duty) {
    return control_set_duty(channel, duty);
}

pwm_driver_result_t control_iface_stop_all(void) {
    return control_stop_all();
}
