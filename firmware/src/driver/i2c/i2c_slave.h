// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

/**
 * @file i2c_slave.h
 * @brief Generic I2C byte transport: frame capture and response transmission.
 *
 * The application supplies callbacks to interpret completed write frames and build read
 * responses. Both callbacks run in the I2C ISR and must be bounded. A callback may perform
 * a short cross-core synchronization; this can briefly stretch SCL.
 */

#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <stdbool.h>
#include <stdint.h>

#define I2C_SLAVE_FRAME_CAPACITY 16u
#define I2C_SLAVE_RESPONSE_CAPACITY 64u

/** @brief Protocol callbacks supplied by the application. */
typedef struct {
    /**
     * @brief Receive one completed write transaction.
     * @param frame Captured bytes, including any protocol-defined address/register byte.
     * @param length Number of bytes stored in @p frame.
     * @param overflow True if the transaction exceeded `I2C_SLAVE_FRAME_CAPACITY`.
     *
     * Runs in the I2C ISR at STOP, repeated START, a new receive phase, or read request.
     * The frame storage is owned by the driver and is valid only until this callback returns.
     * Must not perform unbounded work.
     */
    void (*write_frame)(const uint8_t *frame, uint8_t length, bool overflow);
    /**
     * @brief Build one response for a newly-started read transaction.
     * @param response Caller-owned response buffer.
     * @param capacity Capacity of @p response.
     * @param length Receives the number of response bytes.
     * @return True when a response was built; false requests an empty, zero-padded response.
     *
     * Runs in the I2C ISR and must not perform unbounded work. A short lock wait may stretch SCL.
     */
    bool (*read_response)(uint8_t *response, uint8_t capacity, uint8_t *length);
} i2c_slave_ops_t;

/**
 * @brief Start the slave at @p address using callbacks in @p ops.
 * @param address 7-bit I2C slave address.
 * @param ops Non-null callback table with both functions set; it must outlive the slave.
 */
void i2c_slave_init(uint8_t address, const i2c_slave_ops_t *ops);

/** @brief Return the 7-bit address passed to `i2c_slave_init()`. */
uint8_t i2c_slave_address(void);

#endif
