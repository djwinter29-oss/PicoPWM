// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

/**
 * @file i2c_slave_hw.c
 * @brief RP2040/RP2350 I2C slave peripheral port used by `i2c_slave.c`.
 */

#include "driver/i2c/i2c_slave_hw.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"

/** @brief Interrupts that are always unmasked; TX_EMPTY is added only by `i2c_slave_hw_set_tx_empty_irq()`. */
#define I2C_SLAVE_BASE_MASK                                                                                            \
    (I2C_IC_INTR_MASK_M_RX_FULL_BITS | I2C_IC_INTR_MASK_M_RD_REQ_BITS | I2C_IC_INTR_MASK_M_STOP_DET_BITS |             \
     I2C_IC_INTR_MASK_M_TX_ABRT_BITS | I2C_IC_INTR_MASK_M_RESTART_DET_BITS)

static i2c_hw_t *i2c_slave_hw(void) {
    return i2c_get_hw(i2c_board_config.instance);
}

void i2c_slave_hw_init(uint8_t address, void (*isr)(void)) {
    gpio_init(i2c_board_config.sda_pin);
    gpio_init(i2c_board_config.scl_pin);
    gpio_set_function(i2c_board_config.sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(i2c_board_config.scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(i2c_board_config.sda_pin);
    gpio_pull_up(i2c_board_config.scl_pin);

    // The clock speed only configures internal timing (filtering, edge detection);
    // the bus speed is set by the master, so this must match the expected master clock.
    i2c_init(i2c_board_config.instance, i2c_board_config.clock_hz);
    i2c_set_slave_mode(i2c_board_config.instance, true, address);
    i2c_slave_hw_set_tx_empty_irq(false);

    irq_set_exclusive_handler(i2c_board_config.irq, isr);
    irq_set_enabled(i2c_board_config.irq, true);
}

bool i2c_slave_hw_rx_pop(uint8_t *byte, bool *first_data_byte) {
    i2c_hw_t *hw = i2c_slave_hw();
    uint32_t data_cmd;

    if (hw->rxflr == 0u) {
        return false;
    }
    data_cmd = hw->data_cmd;
    *byte = (uint8_t)(data_cmd & 0xFFu);
    *first_data_byte = (data_cmd & I2C_IC_DATA_CMD_FIRST_DATA_BYTE_BITS) != 0u;
    return true;
}

void i2c_slave_hw_tx_push(uint8_t byte) {
    i2c_slave_hw()->data_cmd = byte;
}

uint32_t i2c_slave_hw_events(void) {
    i2c_hw_t *hw = i2c_slave_hw();
    uint32_t status;
    uint32_t events = 0u;

    /*
     * Never use IC_CLR_INTR: it also clears STOP_DET and RD_REQ that assert after this
     * sample. Those bits stay set until `i2c_slave_hw_clear()` reads their own register.
     */
    status = hw->intr_stat;
    events |= (status & I2C_IC_INTR_STAT_R_STOP_DET_BITS) != 0u ? I2C_SLAVE_EVENT_STOP : 0u;
    events |= (status & I2C_IC_INTR_STAT_R_RD_REQ_BITS) != 0u ? I2C_SLAVE_EVENT_RD_REQ : 0u;
    events |= (status & I2C_IC_INTR_STAT_R_TX_EMPTY_BITS) != 0u ? I2C_SLAVE_EVENT_TX_EMPTY : 0u;
    events |= (status & I2C_IC_INTR_STAT_R_RESTART_DET_BITS) != 0u ? I2C_SLAVE_EVENT_RESTART : 0u;
    events |= (status & I2C_IC_INTR_STAT_R_TX_ABRT_BITS) != 0u ? I2C_SLAVE_EVENT_TX_ABORT : 0u;
    return events;
}

void i2c_slave_hw_clear(uint32_t events) {
    i2c_hw_t *hw = i2c_slave_hw();

    if ((events & I2C_SLAVE_EVENT_STOP) != 0u) {
        (void)hw->clr_stop_det;
    }
    if ((events & I2C_SLAVE_EVENT_RD_REQ) != 0u) {
        (void)hw->clr_rd_req;
    }
    if ((events & I2C_SLAVE_EVENT_RESTART) != 0u) {
        (void)hw->clr_restart_det;
    }
    if ((events & I2C_SLAVE_EVENT_TX_ABORT) != 0u) {
        (void)hw->clr_tx_abrt;
    }
}

void i2c_slave_hw_set_tx_empty_irq(bool enabled) {
    i2c_slave_hw()->intr_mask = I2C_SLAVE_BASE_MASK | (enabled ? I2C_IC_INTR_MASK_M_TX_EMPTY_BITS : 0u);
}
