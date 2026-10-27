/**
 * @file device_service.h
 * @brief Shared Core 0 device API surface for CDC, I2C, and HID transports.
 */

#ifndef DEVICE_SERVICE_H
#define DEVICE_SERVICE_H

#include "driver/pwm/pwm_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Return the fixed device name exposed by control/status transports. */
const char *device_service_device_name(void);

/** @brief Return the build-time firmware version exposed by control/status transports. */
const char *device_service_firmware_version(void);

/** @brief Publish the running and target startup configurations to the device facade. */
bool device_service_config_init(const pwm_driver_config_t *running, const pwm_driver_config_t *target);

/** @brief Return the target startup configuration. */
bool device_service_config_get_target(pwm_driver_config_t *config);

/** @brief Return the immutable running startup configuration. */
bool device_service_config_get_running(pwm_driver_config_t *config);

/** @brief Change one target bank without affecting the running configuration. */
bool device_service_config_set_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_backend_t backend,
                                pwm_driver_config_bank_role_t role);

/** @brief Change only the target I2C address; it applies after save and reboot. */
bool device_service_config_set_i2c_address(uint8_t address);

/** @brief Persist the target configuration for the next reboot. */
bool device_service_config_save_target(void);

/** @brief Return whether the target configuration differs from the saved copy (defaults when none is saved). */
bool device_service_config_target_dirty(void);

/** @brief Copy the 8-byte SDK board ID, in the byte order of the USB serial string. */
void device_service_get_uid(uint8_t uid[8]);

/** @brief Return the running system clock in Hz. */
uint32_t device_service_system_clock_hz(void);

/** @brief Return the logical PWM channel count exposed by the firmware. */
uint8_t device_service_channel_count(void);

/**
 * @brief Read one logical channel snapshot.
 * @param channel Logical channel index.
 * @param state Caller-owned destination for the realized channel state.
 * @return `true` when the channel exists and the state was copied.
 */
bool device_service_get_channel(uint channel, pwm_driver_state_t *state);

/**
 * @brief Apply both frequency and duty to one logical channel.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`; values above `100` are clamped.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t device_service_set_channel(uint channel, uint32_t freq_hz, uint8_t duty);

/**
 * @brief Restore all logical channels to the shared default state.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t device_service_restore_defaults(void);

#endif