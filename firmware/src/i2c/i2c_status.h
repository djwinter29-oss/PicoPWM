/**
 * @file i2c_status.h
 * @brief Per-register I2C write status that keeps a newer attempt from being overwritten.
 */

#ifndef I2C_I2C_STATUS_H
#define I2C_I2C_STATUS_H

#include <stdint.h>

/** @brief Latest write status for one register, with a generation for in-flight completions. */
typedef struct {
    uint32_t epoch; /**< Increments every time a new attempt publishes a status. */
    uint8_t status; /**< Latest status byte visible to an I2C read of this register. */
} i2c_reg_status_t;

/**
 * @brief Publish a new status for one register and retire older completions.
 * @param reg Caller-owned register status slot.
 * @param status Status byte that should win over any in-flight completion.
 */
static inline void i2c_reg_status_set(volatile i2c_reg_status_t *reg, uint8_t status) {
    reg->epoch++;
    reg->status = status;
}

/**
 * @brief Publish a command result only when no newer attempt has replaced it.
 * @param reg Caller-owned register status slot.
 * @param captured_epoch Epoch observed when this command was claimed.
 * @param status Result of the claimed command.
 */
static inline void i2c_reg_status_complete(volatile i2c_reg_status_t *reg, uint32_t captured_epoch, uint8_t status) {
    if (reg->epoch == captured_epoch) {
        reg->status = status;
    }
}

#endif
