/**
 * @file i2c_config.h
 * @brief I2C slave configuration.
 */

#ifndef I2C_CONFIG_H
#define I2C_CONFIG_H

/**
 * I2C peripheral instance.
 * Standard Pico board: i2c1 (GPIO 26 SDA, GPIO 27 SCL).
 */
#define I2C_SLAVE_INST    i2c1

/**
 * I2C slave address (7-bit).
 * Default is 0x40. Can be overridden at build time with CMake:
 * -DPICO_PWM_I2C_ADDR=0x50
 * Valid range: 0x00 to 0x7F (7-bit addresses only).
 */
#define I2C_SLAVE_ADDR PICO_PWM_I2C_ADDR

/**
 * SDA pin (GPIO 26).
 */
#define I2C_SDA_PIN       26

/**
 * SCL pin (GPIO 27).
 */
#define I2C_SCL_PIN       27

/**
 * I2C clock speed in Hz (for internal timing configuration only).
 *
 * The slave does not generate or control the clock—the master does. However,
 * the RP2040's I2C peripheral requires internal timing setup even in slave mode.
 * This value configures:
 * - Clock edge detection sampling rates
 * - Internal filter timing
 * - Peripheral timing registers
 *
 * Set this to match the **expected** master clock speed:
 * - Standard mode: 100000 Hz (100 kHz)
 * - Fast mode: 400000 Hz (400 kHz)
 *
 * **Important**: If the actual master clock differs from this setting, slave timing
 * logic may fail. Specifically:
 * - If slave is configured for 100 kHz but master uses 400 kHz: FAILS (too slow)
 * - If slave is configured for 400 kHz but master uses 100 kHz: WORKS (timing is relaxed)
 *
 * Default is 400000 Hz, which is backward-compatible with 100 kHz masters.
 * Override at build time with CMake: -DPICO_PWM_I2C_CLOCK_SPEED=100000
 */
#define I2C_CLOCK_SPEED PICO_PWM_I2C_CLOCK_SPEED

#endif
