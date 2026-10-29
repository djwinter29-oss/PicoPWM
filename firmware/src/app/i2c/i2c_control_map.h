/**
 * @file i2c_control_map.h
 * @brief I2C register map and protocol helpers layered on the shared control interface.
 */

#ifndef I2C_CONTROL_MAP_H
#define I2C_CONTROL_MAP_H

#include "driver/i2c/i2c_slave.h"
#include "driver/pwm/pwm_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** @brief I2C register and command byte assignments for the PWM control map. */
typedef enum {
    I2C_CONTROL_MAP_REG_INFO = 0x00u,     /**< Read-only protocol and device summary. */
    I2C_CONTROL_MAP_REG_STOP_ALL = 0x01u, /**< Stop every configured generator channel. */
    I2C_CONTROL_MAP_REG_CH_BASE = 0x10u,  /**< Base of channel state registers. */
    I2C_CONTROL_MAP_REG_SET_BASE = 0x30u, /**< Base of channel command registers. */
} i2c_control_map_reg_t;

/** @brief Fixed byte count returned by `REG_INFO`. */
#define I2C_CONTROL_MAP_INFO_SIZE 7u

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
 * @param last_status Last command status tracked by the transport.
 * @param response Caller-owned destination buffer.
 * @param response_capacity Capacity of @p response in bytes.
 * @param response_len Caller-owned destination for the response byte count.
 * @return `true` when the register is supported and the response was built. On `false` the response is untouched.
 */
bool i2c_control_map_read_register(uint8_t reg, uint8_t last_status, uint8_t *response, uint8_t response_capacity,
                                   uint8_t *response_len);

/**
 * @brief Execute one I2C write command payload on Core 0.
 * @param reg I2C register or command byte.
 * @param payload Caller-owned payload bytes excluding @p reg.
 * @param payload_len Payload byte count excluding @p reg.
 * @return Result code from the shared PWM control plane.
 */
pwm_driver_result_t i2c_control_map_execute_write(uint8_t reg, const uint8_t *payload, uint8_t payload_len);

/** @brief This register map expressed as the callbacks `i2c_slave_init()` expects. */
extern const i2c_slave_ops_t i2c_control_map_ops;

#endif
