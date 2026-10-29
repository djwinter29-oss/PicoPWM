/**
 * @file board.c
 * @brief Board-level pin allocation and peripheral settings for the standard Pico board.
 */

#include "driver/i2c/i2c_slave_hw.h"
#include "driver/pwm/pwm_driver.h"

/**
 * @brief I2C slave settings. The slave never drives the bus clock, but the peripheral still needs timing
 * setup: 100 kHz fails with a 400 kHz master, while 400 kHz also works with a 100 kHz master.
 */
const i2c_board_config_t i2c_board_config = {
    .instance = i2c1,
    .irq = I2C1_IRQ,
    .sda_pin = 26u,
    .scl_pin = 27u,
    .clock_hz = 400000u,
};

/** @brief Bank A hardware PWM settings; see docs/guides/hardware-setup.md. */
const hardware_pwm_config_t hardware_pwm_config = {
    .channels = {{1u}, {3u}, {5u}, {7u}, {9u}, {11u}, {13u}, {15u}},
};

/** @brief Bank B PIO PWM settings. */
const pio_pwm_config_t pio_pwm_config = {
    .channels = {{0u}, {2u}, {4u}, {6u}, {8u}, {10u}, {12u}, {14u}},
};

/** @brief Bank C software PWM settings. */
const software_pwm_config_t software_pwm_config = {
    .channels = {{16u}, {17u}, {18u}, {19u}, {20u}, {21u}, {22u}, {28u}},
};
