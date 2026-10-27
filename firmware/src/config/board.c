/**
 * @file board.c
 * @brief Board-level pin allocation and peripheral settings for the standard Pico board.
 */

#include "driver/i2c/i2c_slave.h"
#include "driver/pwm/pwm_driver_config.h"

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

/** @brief PWM pin sets; see docs/guides/pinout.md. */
const uint pwm_board_bank_gpio[PWM_DRIVER_CONFIG_BANK_COUNT][PWM_DRIVER_CONFIG_BANK_SIZE] = {
    [PWM_DRIVER_CONFIG_BANK_A] = {1u, 3u, 5u, 7u, 9u, 11u, 13u, 15u},  /* HW: slice-B GPIOs */
    [PWM_DRIVER_CONFIG_BANK_B] = {0u, 2u, 4u, 6u, 8u, 10u, 12u, 14u},  /* PIO: companion slice-A GPIOs */
    [PWM_DRIVER_CONFIG_BANK_C] = {16u, 17u, 18u, 19u, 20u, 21u, 22u, 28u}, /* SW: any free GPIO */
};
