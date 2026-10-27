#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include "hardware/i2c.h"

#include <stdint.h>

/** @brief Peripheral, GPIO assignment, and timing used by the I2C slave. */
typedef struct {
    i2c_inst_t *instance; /**< I2C peripheral instance. */
    uint irq;             /**< IRQ line paired with `instance`. */
    uint sda_pin;         /**< SDA GPIO; must support the I2C function of `instance`. */
    uint scl_pin;         /**< SCL GPIO; must support the I2C function of `instance`. */
    uint clock_hz;        /**< Internal timing setup only; match the expected master (100000 or 400000). */
} i2c_board_config_t;

/** @brief Board I2C slave configuration, defined in config/board.c. */
extern const i2c_board_config_t i2c_board_config;

void i2c_slave_init(uint8_t address);
/** @brief Return the 7-bit address passed to `i2c_slave_init()`. */
uint8_t i2c_slave_address(void);
void i2c_slave_poll(void);

/**
 * @brief Build a deferred I2C read response and release a stretched clock.
 *
 * Safe to call from Core 0 while a PWM mailbox wait holds the control lock.
 * It does not execute queued writes.
 */
void i2c_slave_service_reads(void);

#endif