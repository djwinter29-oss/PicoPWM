/**
 * @file i2c_control_map.h
 * @brief I2C register map and protocol helpers layered on top of the shared control interface.
 */

#ifndef I2C_CONTROL_MAP_H
#define I2C_CONTROL_MAP_H

#include "pwmdriver/pwm_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief I2C register and command byte assignments for the shared control map. */
typedef enum {
	I2C_CONTROL_MAP_REG_INFO = 0x00u, /**< Read-only register returning the fixed device name string. */
	I2C_CONTROL_MAP_REG_VERSION = 0x01u, /**< Read-only register returning the fixed firmware version string. */
	I2C_CONTROL_MAP_REG_CHANNEL_COUNT = 0x02u, /**< Read-only register returning the logical channel count. */
	I2C_CONTROL_MAP_REG_CH_BASE = 0x10u, /**< Base register for 24 channel snapshot reads, one 12-byte record per channel. */
	I2C_CONTROL_MAP_REG_SET_BASE = 0x30u, /**< Base register for full channel write commands carrying freq and duty. */
	I2C_CONTROL_MAP_REG_SET_FREQ_BASE = 0x50u, /**< Base register for frequency-only write commands. */
	I2C_CONTROL_MAP_REG_SET_DUTY_BASE = 0x70u, /**< Base register for duty-only write commands. */
	I2C_CONTROL_MAP_REG_STOP_ALL = 0x90u, /**< Register used to request stop-all and to read the last command result. */
	I2C_CONTROL_MAP_REG_LED = 0x91u, /**< Register used to set the board LED state and to read the last command result. */
	I2C_CONTROL_MAP_REG_REBOOT = 0x92u, /**< Register used to request a board reboot and to read the last command result. */
} i2c_control_map_reg_t;

/**
 * @brief Return whether one register represents a write-capable command.
 * @param reg I2C register or command byte.
 * @return `true` when the register can schedule a deferred write.
 */
bool i2c_control_map_is_write_register(uint8_t reg);

/**
 * @brief Return the total write length, including register byte, for one I2C register command.
 * @param reg I2C register or command byte.
 * @return Expected total write length, or `0` when the register is unsupported.
 */
uint8_t i2c_control_map_expected_write_length(uint8_t reg);

/**
 * @brief Build the current I2C read response for one register.
 * @param reg I2C register or command byte.
 * @param last_status Last completed write status tracked by the transport.
 * @param response Caller-owned destination buffer.
 * @param response_len Caller-owned destination for the response byte count.
 * @return `true` when the register is supported and the response was built.
 */
bool i2c_control_map_read_register(uint8_t reg, uint8_t last_status, uint8_t *response, uint8_t *response_len);

/**
 * @brief Execute one I2C write command payload on Core 0.
 * @param reg I2C register or command byte.
 * @param payload Caller-owned payload bytes excluding @p reg.
 * @param payload_len Payload byte count excluding @p reg.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t i2c_control_map_execute_write(uint8_t reg, const uint8_t *payload, uint8_t payload_len);

#endif