/**
 * @file device.c
 * @brief Shared Core 0 device API surface for CDC, I2C, and HID transports.
 */

#include "device.h"

#include "pico/unique_id.h"
#include "driver/i2c/i2c_slave.h"
#include "platform/system.h"
#include "platform/led.h"
#include "hardware/regs/addressmap.h"

/** @brief Linker boundaries for static SRAM, scratch data, and reserved core stacks. */
extern char __bss_end__, __scratch_x_start__, __scratch_x_end__, __scratch_y_start__, __scratch_y_end__;
extern char __StackBottom, __StackTop, __StackOneBottom, __StackOneTop;

uint32_t device_ram_reserved_bytes(void) {
    return (uint32_t)((uintptr_t)&__bss_end__ - SRAM_BASE + (uintptr_t)&__scratch_x_end__ -
                      (uintptr_t)&__scratch_x_start__ + (uintptr_t)&__scratch_y_end__ -
                      (uintptr_t)&__scratch_y_start__ + (uintptr_t)&__StackTop - (uintptr_t)&__StackBottom +
                      (uintptr_t)&__StackOneTop - (uintptr_t)&__StackOneBottom);
}

uint32_t device_ram_total_bytes(void) {
    return (uint32_t)((uintptr_t)&__StackTop - SRAM_BASE);
}

uint32_t device_uptime_seconds(void) {
    return (uint32_t)(to_us_since_boot(get_absolute_time()) / 1000000u);
}

int32_t device_temperature_millic(void) {
    return system_temperature_millic();
}

uint16_t device_service_load(bool core1) {
    return core1 ? pwm_driver_service_load() : system_service_load();
}
#include "driver/pwm/pwm_driver_internal.h"

const char *device_name(void) {
    return "PicoPWM";
}

const char *device_firmware_version(void) {
    return PICO_PWM_FIRMWARE_VERSION_STR;
}

void device_get_uid(uint8_t uid[8]) {
    pico_unique_board_id_t id;

    pico_get_unique_board_id(&id);
    for (uint i = 0; i < 8u; ++i) {
        uid[i] = id.id[i];
    }
}

uint32_t device_system_clock_hz(void) {
    return system_clock_hz();
}

void device_schedule_reboot(uint32_t delay_ms) {
    system_schedule_reboot(delay_ms);
}

bool device_led_read(bool *on) {
    return led_read_state(on);
}

bool device_led_set(bool on) {
    bool current;
    if (!led_read_state(&current)) {
        return false;
    }
    led_set(on);
    return true;
}

pwm_role_t device_bank_role(pwm_bank_t bank) {
    pwm_role_t role = PWM_ROLE_MONITOR;

    (void)pwm_driver_config_channel_role((uint)bank * PWM_DRIVER_CONFIG_BANK_SIZE, &role);
    return role;
}

uint8_t device_i2c_address(void) {
    return i2c_slave_address();
}

uint8_t device_channel_count(void) {
    return (uint8_t)PWM_DRIVER_CHANNEL_COUNT;
}

bool device_get_channel(uint channel, pwm_state_t *state) {
    return pwm_driver_get(channel, state);
}

pwm_driver_result_t device_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    return pwm_driver_set(channel, freq_hz, duty);
}

pwm_driver_result_t device_restore_defaults(void) {
    return pwm_driver_restore_defaults();
}
