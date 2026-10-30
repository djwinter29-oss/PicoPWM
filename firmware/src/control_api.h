// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

/**
 * @file control_api.h
 * @brief Shared Core 0 control and device-information API for host interfaces.
 */

#ifndef CONTROL_API_H
#define CONTROL_API_H

#include "driver/pwm/pwm_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Return the fixed device name exposed by control/status transports. */
const char *control_api_name(void);

/** @brief Return the build-time firmware version exposed by control/status transports. */
const char *control_api_firmware_version(void);

/** @brief Copy the 8-byte SDK board ID, in the byte order of the USB serial string. */
void control_api_get_uid(uint8_t uid[8]);

/** @brief Return the running system clock in Hz. */
uint32_t control_api_system_clock_hz(void);

/**
 * @brief Schedule a board reboot from Core 0.
 * @param delay_ms Delay allowing the transport to return its acknowledgement.
 */
void control_api_schedule_reboot(uint32_t delay_ms);

/**
 * @brief Read the identification LED state.
 * @param on Receives the current state.
 * @return True when the board has an LED and the destination is valid.
 */
bool control_api_led_read(bool *on);

/**
 * @brief Set the identification LED.
 * @param on Requested LED state.
 * @return True when the board has an LED.
 */
bool control_api_led_set(bool on);

/** @brief Return static SRAM and reserved core-stack bytes, not live heap/stack usage. */
uint32_t control_api_ram_reserved_bytes(void);

/** @brief Return physical SRAM capacity in bytes. */
uint32_t control_api_ram_total_bytes(void);

/** @brief Return time since boot in seconds, wrapping after UINT32_MAX seconds. */
uint32_t control_api_uptime_seconds(void);

/** @brief Return internal die temperature in millidegrees Celsius, or INT32_MIN on invalid ADC conversion. */
int32_t control_api_temperature_millic(void);

/** @brief Return service-work wall time in basis points for Core 0 (0) or Core 1 (1). */
uint16_t control_api_service_load(bool core1);

/** @brief Role a bank is running with (what the last boot applied). */
pwm_role_t control_api_bank_role(pwm_bank_t bank);

/** @brief 7-bit I2C address the slave is running with. */
uint8_t control_api_i2c_address(void);

/** @brief Return the logical PWM channel count exposed by the firmware. */
uint8_t control_api_channel_count(void);

/** @brief Board pin assignment; UINT8_MAX denotes an unavailable GPIO. */
typedef struct {
    uint8_t channels[PWM_DRIVER_CHANNEL_COUNT]; /**< GPIOs indexed by logical PWM channel. */
    uint8_t i2c_sda;                            /**< I2C slave SDA GPIO. */
    uint8_t i2c_scl;                            /**< I2C slave SCL GPIO. */
    uint8_t led;                                /**< Board LED GPIO, or UINT8_MAX when absent. */
} control_api_pin_assignment_t;

/** @brief Copy the compiled board GPIO allocation into a caller-owned destination. */
void control_api_get_pin_assignment(control_api_pin_assignment_t *pins);

/**
 * @brief Read one logical channel snapshot.
 * @param channel Logical channel index.
 * @param state Caller-owned destination for the realized channel state.
 * @return `true` when the channel exists and the state was copied.
 */
bool control_api_get_channel(uint channel, pwm_state_t *state);

/**
 * @brief Apply both frequency and duty to one logical channel.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`; values above `100` are clamped.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t control_api_set_channel(uint channel, uint32_t freq_hz, uint8_t duty);

/**
 * @brief Stop every configured generator channel; monitor channels are unchanged.
 * @return `PWM_DRIVER_RESULT_OK` if all outputs stopped, otherwise the first failure after all are attempted.
 */
pwm_driver_result_t control_api_restore_defaults(void);

#endif