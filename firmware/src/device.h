/**
 * @file device.h
 * @brief Shared Core 0 device API surface for CDC, I2C, and HID transports.
 */

#ifndef DEVICE_H
#define DEVICE_H

#include "driver/pwm/pwm_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Return the fixed device name exposed by control/status transports. */
const char *device_name(void);

/** @brief Return the build-time firmware version exposed by control/status transports. */
const char *device_firmware_version(void);

/** @brief Copy the 8-byte SDK board ID, in the byte order of the USB serial string. */
void device_get_uid(uint8_t uid[8]);

/** @brief Return the running system clock in Hz. */
uint32_t device_system_clock_hz(void);

/**
 * @brief Schedule a board reboot from Core 0.
 * @param delay_ms Delay allowing the transport to return its acknowledgement.
 */
void device_schedule_reboot(uint32_t delay_ms);

/**
 * @brief Read the identification LED state.
 * @param on Receives the current state.
 * @return True when the board has an LED and the destination is valid.
 */
bool device_led_read(bool *on);

/**
 * @brief Set the identification LED.
 * @param on Requested LED state.
 * @return True when the board has an LED.
 */
bool device_led_set(bool on);

/** @brief Return static SRAM and reserved core-stack bytes, not live heap/stack usage. */
uint32_t device_ram_reserved_bytes(void);

/** @brief Return physical SRAM capacity in bytes. */
uint32_t device_ram_total_bytes(void);

/** @brief Return time since boot in seconds, wrapping after UINT32_MAX seconds. */
uint32_t device_uptime_seconds(void);

/** @brief Return internal die temperature in millidegrees Celsius, or INT32_MIN on invalid ADC conversion. */
int32_t device_temperature_millic(void);

/** @brief Return service-work wall time in basis points for Core 0 (0) or Core 1 (1). */
uint16_t device_service_load(bool core1);

/** @brief Role a bank is running with (what the last boot applied). */
pwm_role_t device_bank_role(pwm_bank_t bank);

/** @brief 7-bit I2C address the slave is running with. */
uint8_t device_i2c_address(void);

/** @brief Return the logical PWM channel count exposed by the firmware. */
uint8_t device_channel_count(void);

/**
 * @brief Read one logical channel snapshot.
 * @param channel Logical channel index.
 * @param state Caller-owned destination for the realized channel state.
 * @return `true` when the channel exists and the state was copied.
 */
bool device_get_channel(uint channel, pwm_state_t *state);

/**
 * @brief Apply both frequency and duty to one logical channel.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`; values above `100` are clamped.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t device_set_channel(uint channel, uint32_t freq_hz, uint8_t duty);

/**
 * @brief Restore all logical channels to the shared default state.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t device_restore_defaults(void);

#endif