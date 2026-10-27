/**
 * @file pwm_driver_config.h
 * @brief Fixed logical PWM driver configuration for PicoPWM.
 */

#ifndef PWM_DRIVER_CONFIG_H
#define PWM_DRIVER_CONFIG_H

#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef PWM_DRIVER_CONFIG_GPIO_COUNT
#define PWM_DRIVER_CONFIG_GPIO_COUNT 30u
#endif

#ifndef PWM_DRIVER_CONFIG_REQUIRE_HW_CHANNEL_B
#define PWM_DRIVER_CONFIG_REQUIRE_HW_CHANNEL_B 1
#endif

/** @brief Number of logical channels exposed by the selected profile. */
#define PWM_DRIVER_CONFIG_CHANNEL_COUNT 24u

/** @brief Number of logical channels owned by one physical bank. */
#define PWM_DRIVER_CONFIG_BANK_SIZE 8u

/** @brief Fixed physical bank; each bank owns 8 stable logical channels. */
typedef enum {
    PWM_DRIVER_CONFIG_BANK_A = 0, /**< HW-capable GPIOs, logical channels 0..7. */
    PWM_DRIVER_CONFIG_BANK_B,     /**< PIO-capable GPIOs, logical channels 8..15. */
    PWM_DRIVER_CONFIG_BANK_C,     /**< Software-only GPIOs, logical channels 16..23. */
    PWM_DRIVER_CONFIG_BANK_COUNT, /**< Number of physical banks. */
} pwm_driver_config_bank_t;

/** @brief Board settings for one hardware PWM channel (bank A). */
typedef struct {
    uint gpio; /**< Slice-B GPIO driven or measured by this channel. */
} hardware_pwm_channel_config_t;

/** @brief Board settings for one PIO PWM channel (bank B). */
typedef struct {
    uint gpio; /**< Companion slice-A GPIO driven or measured by this channel. */
} pio_pwm_channel_config_t;

/** @brief Board settings for one software PWM channel (bank C). */
typedef struct {
    uint gpio; /**< Any GPIO not claimed by the hardware or PIO sets. */
} software_pwm_channel_config_t;

/** @brief Hardware PWM settings (bank A). */
typedef struct {
    hardware_pwm_channel_config_t channels[PWM_DRIVER_CONFIG_BANK_SIZE]; /**< Per-channel settings. */
} hardware_pwm_config_t;

/** @brief PIO PWM settings (bank B). */
typedef struct {
    pio_pwm_channel_config_t channels[PWM_DRIVER_CONFIG_BANK_SIZE]; /**< Per-channel settings. */
} pio_pwm_config_t;

/** @brief Software PWM settings (bank C). */
typedef struct {
    software_pwm_channel_config_t channels[PWM_DRIVER_CONFIG_BANK_SIZE]; /**< Per-channel settings. */
} software_pwm_config_t;

/**
 * @brief Static PWM board settings, defined in config/board.c.
 *
 * Bank A is hardware PWM, bank B is PIO and bank C is software; only the role is selectable.
 */
extern const hardware_pwm_config_t hardware_pwm_config;
extern const pio_pwm_config_t pio_pwm_config;
extern const software_pwm_config_t software_pwm_config;

/** @brief Requested role when locking one bank. */
typedef enum {
    PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR = 0, /**< Bank channels become PWM outputs. */
    PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,       /**< Bank channels become PWM inputs. */
} pwm_driver_config_bank_role_t;

/** @brief Return the configured role of the bank that owns one logical channel; false when unavailable. */
bool pwm_driver_config_channel_role(uint channel, pwm_driver_config_bank_role_t *role);

/** @brief Return a short name ("HW", "PIO-MON", ...) for the module that owns one logical channel. */
const char *pwm_driver_config_channel_name(uint channel);

/** @brief Validate board GPIO ownership; run when the bank roles are configured. */
bool pwm_driver_config_validate(void);

/** @brief Return whether a profile channel accepts one requested frequency. */
bool pwm_driver_config_frequency_supported(uint channel, uint32_t frequency_hz);

/** @brief Return whether the selected profile is input-monitoring oriented. */
bool pwm_driver_config_is_monitor(void);

#endif
