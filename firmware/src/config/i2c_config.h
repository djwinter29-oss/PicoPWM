/**
 * @file i2c_config.h
 * @brief I2C slave board configuration table; values live in i2c_config.c.
 */

#ifndef I2C_CONFIG_H
#define I2C_CONFIG_H

#include "hardware/i2c.h"

/** @brief Peripheral, GPIO assignment, and timing used by the I2C slave. */
typedef struct {
    i2c_inst_t *instance; /**< I2C peripheral instance. */
    uint irq;             /**< IRQ line paired with `instance`. */
    uint sda_pin;         /**< SDA GPIO; must support the I2C function of `instance`. */
    uint scl_pin;         /**< SCL GPIO; must support the I2C function of `instance`. */
    uint clock_hz;        /**< Internal timing setup only; match the expected master (100000 or 400000). */
} i2c_board_config_t;

/** @brief Board I2C slave configuration; the clock comes from `PICO_PWM_I2C_CLOCK_SPEED`. */
extern const i2c_board_config_t i2c_board_config;

#endif
