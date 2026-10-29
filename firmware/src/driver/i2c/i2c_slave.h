/**
 * @file i2c_slave.h
 * @brief I2C register transport: ISR byte capture, deferred writes, and ISR-built read responses.
 *
 * The transport knows nothing about the register map; the application supplies it as an
 * `i2c_slave_ops_t`. Writes are queued by the ISR and executed from `i2c_slave_poll()`
 * on Core 0. Reads are answered inside the ISR, so `ops->read` must not block.
 */

#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Status bytes a master reads back from a write register. */
typedef enum {
    I2C_STATUS_OK = 0u,          /**< The command completed successfully. */
    I2C_STATUS_BUSY = 1u,        /**< The command is queued or still being applied. */
    I2C_STATUS_INVALID = 2u,     /**< The command or its framing is invalid. */
    I2C_STATUS_UNAVAILABLE = 3u, /**< The operation is unavailable or command queue is full. */
    I2C_STATUS_TIMEOUT = 4u,     /**< The channel command was not claimed before its deadline. */
    I2C_STATUS_APPLY_FAILED = 5u, /**< The backend rejected an admitted channel command. */
    I2C_STATUS_NOT_RUN = 6u,     /**< No command attempt has been recorded since startup. */
} i2c_status_t;

/** @brief Register map callbacks supplied by the application. */
typedef struct {
    /** @brief Expected write length including the register byte, or 0 for an unsupported register. */
    uint8_t (*write_length)(uint8_t reg);
    /** @brief True when a complete write to @p reg is a command to queue rather than a read select. */
    bool (*is_write)(uint8_t reg);
    /** @brief Build the read response for @p reg. Runs in the ISR; returns false when unsupported. */
    bool (*read)(uint8_t reg, uint8_t last_status, uint8_t *response, uint8_t capacity, uint8_t *length);
    /** @brief Execute one queued write on Core 0 and return its final `i2c_status_t`. */
    uint8_t (*execute)(uint8_t reg, const uint8_t *payload, uint8_t length);
} i2c_slave_ops_t;

/** @brief Start the slave at @p address using the register map @p ops, which must outlive the slave. */
void i2c_slave_init(uint8_t address, const i2c_slave_ops_t *ops);

/** @brief Return the 7-bit address passed to `i2c_slave_init()`. */
uint8_t i2c_slave_address(void);

/** @brief Execute at most one queued write. Call from the Core 0 loop. */
void i2c_slave_poll(void);

#endif
