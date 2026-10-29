/**
 * @file i2c_slave_hw.h
 * @brief SDK-facing port of the I2C slave peripheral.
 *
 * `i2c_slave.c` holds the protocol and ISR logic and reaches the peripheral only
 * through these calls, so host tests can replace them with a fake FIFO.
 */

#ifndef I2C_SLAVE_HW_H
#define I2C_SLAVE_HW_H

#include "hardware/i2c.h"

#include <stdbool.h>
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

/** @brief Pending slave events reported by `i2c_slave_hw_events()`. */
typedef enum {
    I2C_SLAVE_EVENT_STOP = 1u << 0,     /**< The master ended the transaction. */
    I2C_SLAVE_EVENT_RD_REQ = 1u << 1,   /**< The master is reading and needs a byte. */
    I2C_SLAVE_EVENT_TX_EMPTY = 1u << 2, /**< The TX FIFO is empty while TX_EMPTY is unmasked. */
    I2C_SLAVE_EVENT_RESTART = 1u << 3,  /**< The master started a new transaction without STOP. */
} i2c_slave_event_t;

/** @brief Configure pins and the peripheral as a slave and install @p isr on the I2C IRQ. */
void i2c_slave_hw_init(uint8_t address, void (*isr)(void));

/** @brief Pop one received byte. Returns false when the RX FIFO is empty. */
bool i2c_slave_hw_rx_pop(uint8_t *byte);

/** @brief Queue one byte for the master to read. */
void i2c_slave_hw_tx_push(uint8_t byte);

/** @brief Return the pending `i2c_slave_event_t` bits. Also releases a master NACK (TX_ABRT). */
uint32_t i2c_slave_hw_events(void);

/** @brief Clear latched STOP, RD_REQ, and RESTART events in @p events. Other bits are ignored. */
void i2c_slave_hw_clear(uint32_t events);

/**
 * @brief Gate the level-triggered TX_EMPTY interrupt.
 *
 * TX_EMPTY follows an empty FIFO and `clr_intr` does not clear it, so it stays masked
 * unless a read still has bytes to send. Leaving it unmasked holds the I2C IRQ and stops Core 0.
 */
void i2c_slave_hw_set_tx_empty_irq(bool enabled);

#endif
