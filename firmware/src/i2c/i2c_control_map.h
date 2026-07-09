/**
 * @file i2c_control_map.h
 * @brief I2C register map and protocol helpers layered on top of the shared control interface.
 */

#ifndef I2C_CONTROL_MAP_H
#define I2C_CONTROL_MAP_H

#include "pwmdriver/pwm_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief Read-only I2C register returning the fixed device name string. */
#define I2C_CONTROL_MAP_REG_INFO          0x00u
/** @brief Read-only I2C register returning the fixed firmware version string. */
#define I2C_CONTROL_MAP_REG_VERSION       0x01u
/** @brief Read-only I2C register returning the logical channel count. */
#define I2C_CONTROL_MAP_REG_CHANNEL_COUNT 0x02u
/** @brief Base register for 24 channel snapshot reads, one 12-byte record per channel. */
#define I2C_CONTROL_MAP_REG_CH_BASE       0x10u
/** @brief Base register for full channel write commands carrying freq and duty. */
#define I2C_CONTROL_MAP_REG_SET_BASE      0x30u
/** @brief Base register for frequency-only write commands. */
#define I2C_CONTROL_MAP_REG_SET_FREQ_BASE 0x50u
/** @brief Base register for duty-only write commands. */
#define I2C_CONTROL_MAP_REG_SET_DUTY_BASE 0x70u
/** @brief Register used to request stop-all and to read the last command result. */
#define I2C_CONTROL_MAP_REG_STOP_ALL      0x90u

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