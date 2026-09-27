/**
 * @file i2c_irq_gate.h
 * @brief Decisions for the I2C slave TX_EMPTY mask.
 *
 * TX_EMPTY is level-triggered and follows an empty transmit FIFO. clr_intr does
 * not clear it. Leaving it unmasked while idle, or after the last response byte,
 * holds the I2C IRQ and stops the Core 0 loop. The mask is therefore off until
 * a read still has bytes to send, and it is turned off again on the last byte
 * and on STOP.
 */

#ifndef I2C_IRQ_GATE_H
#define I2C_IRQ_GATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Runtime mask state owned by the slave ISR. */
typedef struct {
    bool tx_empty_unmasked; /**< True only while a read still needs further FIFO bytes. */
    bool irq_enabled;       /**< False while Core 0 is stretching SCL to prepare a response. */
} i2c_irq_gate_t;

/** @brief What the ISR should do for one read-request or TX_EMPTY event. */
typedef enum {
    I2C_GATE_NONE = 0,   /**< No FIFO write. */
    I2C_GATE_STRETCH,    /**< Leave RD_REQ pending and disable the I2C IRQ. */
    I2C_GATE_WRITE_BYTE, /**< Write the next prepared response byte. */
    I2C_GATE_PAD_BYTE,   /**< Master clocked past the response; write a zero pad. */
    I2C_GATE_MASK_TX,    /**< No byte remains; mask TX_EMPTY before leaving the ISR. */
} i2c_gate_action_t;

/** @brief Start with TX_EMPTY masked and the IRQ enabled. */
static inline void i2c_irq_gate_init(i2c_irq_gate_t *gate) {
    if (gate == NULL) {
        return;
    }
    gate->tx_empty_unmasked = false;
    gate->irq_enabled = true;
}

/** @brief Re-enable the I2C IRQ after Core 0 has prepared a stretched response. */
static inline void i2c_irq_gate_resume(i2c_irq_gate_t *gate) {
    if (gate == NULL) {
        return;
    }
    gate->irq_enabled = true;
}

/** @brief Mask TX_EMPTY when the master ends the transaction. */
static inline void i2c_irq_gate_on_stop(i2c_irq_gate_t *gate) {
    if (gate == NULL) {
        return;
    }
    gate->tx_empty_unmasked = false;
}

/**
 * @brief Decide how to answer RD_REQ.
 * @param response_pending True when Core 0 has been asked to build a response.
 * @param index Next response byte index.
 * @param length Prepared response length.
 */
static inline i2c_gate_action_t i2c_irq_gate_on_read_request(i2c_irq_gate_t *gate, bool response_ready,
                                                             bool response_pending, uint8_t index, uint8_t length) {
    if (gate == NULL) {
        return I2C_GATE_NONE;
    }
    if (!response_ready) {
        gate->tx_empty_unmasked = false;
        if (response_pending) {
            gate->irq_enabled = false;
            return I2C_GATE_STRETCH;
        }
        /* Nothing was requested. Release SCL with a pad byte instead of holding the bus. */
        gate->irq_enabled = true;
        return I2C_GATE_PAD_BYTE;
    }

    gate->irq_enabled = true;
    if (index < length) {
        gate->tx_empty_unmasked = (uint8_t)(index + 1u) < length;
        return I2C_GATE_WRITE_BYTE;
    }
    gate->tx_empty_unmasked = false;
    return I2C_GATE_PAD_BYTE;
}

/**
 * @brief Decide how to answer TX_EMPTY after the first response byte.
 * @param index Next response byte index.
 * @param length Prepared response length.
 */
static inline i2c_gate_action_t i2c_irq_gate_on_tx_empty(i2c_irq_gate_t *gate, bool response_ready, uint8_t index,
                                                         uint8_t length) {
    if (gate == NULL) {
        return I2C_GATE_NONE;
    }
    if (!response_ready || index >= length) {
        gate->tx_empty_unmasked = false;
        return I2C_GATE_MASK_TX;
    }

    gate->tx_empty_unmasked = (uint8_t)(index + 1u) < length;
    return I2C_GATE_WRITE_BYTE;
}

#endif
