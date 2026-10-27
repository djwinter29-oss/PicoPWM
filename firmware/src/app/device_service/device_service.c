/**
 * @file device_service.c
 * @brief Shared Core 0 device API surface for CDC, I2C, and HID transports.
 */

#include "device_service.h"

#include "pico/unique_id.h"
#include "platform/system.h"
#include "driver/pwm/pwm_driver_internal.h"

const char *device_service_device_name(void) {
    return "PicoPWM";
}

const char *device_service_firmware_version(void) {
    return PICO_PWM_FIRMWARE_VERSION_STR;
}

void device_service_get_uid(uint8_t uid[8]) {
    pico_unique_board_id_t id;

    pico_get_unique_board_id(&id);
    for (uint i = 0; i < 8u; ++i) {
        uid[i] = id.id[i];
    }
}

uint32_t device_service_system_clock_hz(void) {
    return system_clock_hz();
}

uint8_t device_service_channel_count(void) {
    return (uint8_t)PWM_DRIVER_CHANNEL_COUNT;
}

bool device_service_get_channel(uint channel, pwm_driver_state_t *state) {
    return pwm_driver_get(channel, state);
}

pwm_driver_result_t device_service_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    return pwm_driver_set(channel, freq_hz, duty);
}

pwm_driver_result_t device_service_restore_defaults(void) {
    return pwm_driver_restore_defaults();
}
