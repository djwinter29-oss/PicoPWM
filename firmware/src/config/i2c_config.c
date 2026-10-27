/**
 * @file i2c_config.c
 * @brief I2C slave pin allocation and timing for the standard Pico board.
 *
 * The slave never drives the bus clock, but the peripheral still needs timing setup. A slave set
 * to 100 kHz fails with a 400 kHz master; one set to 400 kHz also works with a 100 kHz master.
 */

#include "driver/i2c/i2c_slave.h"

const i2c_board_config_t i2c_board_config = {
    .instance = i2c1,
    .irq = I2C1_IRQ,
    .sda_pin = 26u,
    .scl_pin = 27u,
    .clock_hz = PICO_PWM_I2C_CLOCK_SPEED,
};
