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

bool device_service_config_init(const pwm_driver_config_t *running, const pwm_driver_config_t *target) {
    return pwm_driver_config_init_state(running, target);
}

bool device_service_config_get_target(pwm_driver_config_t *config) {
    return pwm_driver_config_get_target(config);
}

bool device_service_config_get_running(pwm_driver_config_t *config) {
    return pwm_driver_config_get_running(config);
}

bool device_service_config_set_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_backend_t backend,
                                pwm_driver_config_bank_role_t role) {
    return pwm_driver_config_set_bank(bank, backend, role);
}

bool device_service_config_save_target(void) {
    pwm_driver_config_t target;
    return pwm_driver_config_get_target(&target) && pwm_driver_config_save_target(&target);
}

bool device_service_config_set_i2c_address(uint8_t address) {
    return pwm_driver_config_set_i2c_address(address);
}

bool device_service_config_target_dirty(void) {
    pwm_driver_config_t target;
    pwm_driver_config_t saved;

    if (!pwm_driver_config_get_target(&target)) {
        return false;
    }
    (void)pwm_driver_config_load_target(&saved);
    return target.bank_a_backend != saved.bank_a_backend || target.bank_b_backend != saved.bank_b_backend ||
           target.bank_c_backend != saved.bank_c_backend || target.bank_a_role != saved.bank_a_role ||
           target.bank_b_role != saved.bank_b_role || target.bank_c_role != saved.bank_c_role ||
           target.i2c_address != saved.i2c_address;
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
