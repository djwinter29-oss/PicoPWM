/**
 * @file board_config.h
 * @brief Board identity and build-level firmware configuration.
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/** @brief Device name reported through CDC and I2C. */
#ifndef BOARD_DEVICE_NAME
#define BOARD_DEVICE_NAME "PicoPWM"
#endif

/** @brief Firmware version reported through CDC and I2C. */
#ifndef PICO_PWM_FIRMWARE_VERSION_STR
#define PICO_PWM_FIRMWARE_VERSION_STR "0.0.0-dev"
#endif

#endif
