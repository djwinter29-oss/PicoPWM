/**
 * @file device_api.h
 * @brief Shared Core 0 device API surface for CDC and I2C transports.
 */

#ifndef DEVICE_API_H
#define DEVICE_API_H

#include "pwmdriver/pwm_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Return the fixed device name exposed by control/status transports. */
const char *device_api_device_name(void);

/** @brief Return the build-time firmware version exposed by control/status transports. */
const char *device_api_firmware_version(void);

/** @brief Publish the running and target startup configurations to the device facade. */
bool device_api_config_init(const pwm_driver_config_t *running, const pwm_driver_config_t *target);

/** @brief Return the target startup configuration. */
bool device_api_config_get_target(pwm_driver_config_t *config);

/** @brief Return the immutable running startup configuration. */
bool device_api_config_get_running(pwm_driver_config_t *config);

/** @brief Change one target bank without affecting the running configuration. */
bool device_api_config_set_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_backend_t backend,
                                pwm_driver_config_bank_role_t role);

/** @brief Change only the target I2C address; it applies after save and reboot. */
bool device_api_config_set_i2c_address(uint8_t address);

/** @brief Persist the target configuration for the next reboot. */
bool device_api_config_save_target(void);

/** @brief Return the logical PWM channel count exposed by the firmware. */
uint8_t device_api_channel_count(void);

/**
 * @brief Read one logical channel snapshot.
 * @param channel Logical channel index.
 * @param state Caller-owned destination for the realized channel state.
 * @return `true` when the channel exists and the state was copied.
 */
bool device_api_get_channel(uint channel, pwm_driver_state_t *state);

/**
 * @brief Apply both frequency and duty to one logical channel.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`; values above `100` are clamped.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t device_api_set_channel(uint channel, uint32_t freq_hz, uint8_t duty);

/**
 * @brief Restore all logical channels to the shared default state.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t device_api_restore_defaults(void);

#endif