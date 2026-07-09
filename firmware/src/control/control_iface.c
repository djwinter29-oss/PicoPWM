/**
 * @file control_iface.c
 * @brief Shared Core 0 control/status interface for CDC and I2C transports.
 */

#include "control/control_iface.h"

/** @brief Fixed device name reported through control/status transports. */
#define CONTROL_IFACE_DEVICE_NAME "PicoPWM"
/** @brief Fixed firmware version reported through control/status transports. */
#define CONTROL_IFACE_FIRMWARE_VERSION "1.0.0"

const char *control_iface_device_name(void) {
    return CONTROL_IFACE_DEVICE_NAME;
}

const char *control_iface_firmware_version(void) {
    return CONTROL_IFACE_FIRMWARE_VERSION;
}

uint8_t control_iface_channel_count(void) {
    return (uint8_t)PWM_DRIVER_CHANNEL_COUNT;
}

bool control_iface_get_channel(uint channel, pwm_driver_state_t *state) {
    return control_get(channel, state);
}

pwm_driver_result_t control_iface_set_channel(uint channel, float freq_hz, float duty) {
    return control_set(channel, freq_hz, duty);
}

pwm_driver_result_t control_iface_set_channel_freq(uint channel, float freq_hz) {
    return control_set_freq(channel, freq_hz);
}

pwm_driver_result_t control_iface_set_channel_duty(uint channel, float duty) {
    return control_set_duty(channel, duty);
}

pwm_driver_result_t control_iface_stop_all(void) {
    return control_stop_all();
}
