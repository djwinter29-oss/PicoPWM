/**
 * @file control_iface.h
 * @brief Shared Core 0 control/status interface for CDC and I2C transports.
 */

#ifndef CONTROL_IFACE_H
#define CONTROL_IFACE_H

#include "pwmdriver/pwm_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Return the fixed device name exposed by control/status transports. */
const char *control_iface_device_name(void);

/** @brief Return the fixed firmware version exposed by control/status transports. */
const char *control_iface_firmware_version(void);

/** @brief Return the logical PWM channel count exposed by the firmware. */
uint8_t control_iface_channel_count(void);

/**
 * @brief Read one logical channel snapshot.
 * @param channel Logical channel index.
 * @param state Caller-owned destination for the realized channel state.
 * @return `true` when the channel exists and the state was copied.
 */
bool control_iface_get_channel(uint channel, pwm_driver_state_t *state);

/**
 * @brief Apply both frequency and duty to one logical channel.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in the normalized range `[0.0, 1.0]`.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t control_iface_set_channel(uint channel, float freq_hz, float duty);

/**
 * @brief Apply frequency while preserving the currently realized duty.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t control_iface_set_channel_freq(uint channel, float freq_hz);

/**
 * @brief Apply duty while preserving the currently realized frequency.
 * @param channel Logical channel index.
 * @param duty Requested duty in the normalized range `[0.0, 1.0]`.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t control_iface_set_channel_duty(uint channel, float duty);

/**
 * @brief Stop all channels and restore the shared default state.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t control_iface_stop_all(void);

#endif