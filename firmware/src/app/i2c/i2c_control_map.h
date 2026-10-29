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

/** @brief Result bytes used by PWM I2C command-status reads. */
typedef enum {
    I2C_STATUS_OK = 0u,          /**< The command completed successfully. */
    I2C_STATUS_BUSY = 1u,        /**< The command is queued or still being applied. */
    I2C_STATUS_INVALID = 2u,     /**< The command or its framing is invalid. */
    I2C_STATUS_UNAVAILABLE = 3u, /**< The operation is unavailable or command queue is full. */
    I2C_STATUS_TIMEOUT = 4u,     /**< The channel command was not claimed before its deadline. */
    I2C_STATUS_APPLY_FAILED = 5u, /**< The backend rejected an admitted channel command. */
    I2C_STATUS_NOT_RUN = 6u,     /**< No command attempt has been recorded since startup. */
} i2c_status_t;

/** @brief I2C register and command byte assignments for the PWM control map. */
typedef enum {
    I2C_CONTROL_MAP_REG_INFO = 0x00u,     /**< Read-only protocol and device summary. */
    I2C_CONTROL_MAP_REG_STOP_ALL = 0x01u, /**< Stop every configured generator channel. */
    I2C_CONTROL_MAP_REG_CH_BASE = 0x10u,  /**< Base of channel state registers. */
    I2C_CONTROL_MAP_REG_SET_BASE = 0x30u, /**< Base of channel command registers. */
} i2c_control_map_reg_t;

/** @brief Fixed byte count returned by `REG_INFO`. */
#define I2C_CONTROL_MAP_INFO_SIZE 7u
/** @brief Frequency and duty bytes in a channel command payload. */
#define I2C_CONTROL_MAP_CHANNEL_COMMAND_PAYLOAD_SIZE 5u

/** @brief Reset protocol selection, command queue, and command status for startup. */
void i2c_control_map_init(void);

/** @brief Execute at most one queued command; call from the Core 0 foreground loop. */
void i2c_control_map_poll(void);

/** @brief PWM register protocol callbacks for the generic I2C transport. */
extern const i2c_slave_ops_t i2c_control_map_ops;

#endif
