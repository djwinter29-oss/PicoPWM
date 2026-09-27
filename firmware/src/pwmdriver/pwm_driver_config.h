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

#ifndef PWM_DRIVER_CONFIG_RESERVED_GPIO_MASK
#define PWM_DRIVER_CONFIG_RESERVED_GPIO_MASK ((1u << 26) | (1u << 27) | (1u << 25))
#endif

#ifndef PWM_DRIVER_CONFIG_REQUIRE_HW_CHANNEL_B
#define PWM_DRIVER_CONFIG_REQUIRE_HW_CHANNEL_B 1
#endif

/** @brief Backend implementation that owns one logical profile channel. */
typedef enum {
    PWM_DRIVER_CONFIG_BACKEND_HW_GENERATOR = 0, /**< Hardware PWM slice output, fixed to the 8 slice-B GPIOs. */
    PWM_DRIVER_CONFIG_BACKEND_PIO_GENERATOR, /**< PIO state-machine output, fixed to the 8 companion slice-A GPIOs. */
    PWM_DRIVER_CONFIG_BACKEND_SW_GENERATOR, /**< Software timer-driven output on any unclaimed GPIO. */
    PWM_DRIVER_CONFIG_BACKEND_HW_MONITOR, /**< Hardware-bank GPIO edge-timestamp input measurement. */
    PWM_DRIVER_CONFIG_BACKEND_PIO_MONITOR, /**< PIO-bank GPIO edge/DMA input measurement. */
    PWM_DRIVER_CONFIG_BACKEND_SW_MONITOR, /**< Software GPIO edge-timestamp input measurement on any unclaimed GPIO. */
} pwm_driver_config_backend_t;

/** @brief Signal direction for one logical profile channel. */
typedef enum {
    PWM_DRIVER_CONFIG_DIRECTION_OUTPUT = 0, /**< Channel generates a PWM output signal. */
    PWM_DRIVER_CONFIG_DIRECTION_INPUT, /**< Channel measures a PWM input signal. */
    PWM_DRIVER_CONFIG_DIRECTION_DISABLED, /**< Channel slot is unused by the selected profile. */
} pwm_driver_config_direction_t;

/** @brief Bitmask of operations one logical profile channel supports. */
typedef enum {
    PWM_DRIVER_CONFIG_CAP_READ = 1u << 0, /**< Channel state can be read back. */
    PWM_DRIVER_CONFIG_CAP_SET = 1u << 1, /**< Channel frequency/duty can be written. */
} pwm_driver_config_capability_t;

/** @brief Build-time descriptor for one logical PWM channel. */
typedef struct {
    pwm_driver_config_backend_t backend; /**< Backend implementation that owns this channel. */
    pwm_driver_config_direction_t direction; /**< Output, input, or disabled. */
    uint gpio; /**< Physical GPIO assigned to this channel; ignored when disabled. */
    uint backend_channel; /**< Backend-local index this channel maps to. */
    uint8_t capabilities; /**< Bitmask of `pwm_driver_config_capability_t` operations supported. */
    uint32_t min_frequency_hz; /**< Minimum generated or measurable nonzero frequency. */
    uint32_t max_frequency_hz; /**< Maximum generated or measurable frequency. */
    uint32_t accuracy_ppm; /**< Expected generation or measurement accuracy in parts-per-million. */
} pwm_driver_config_channel_t;

/** @brief Number of logical channels exposed by the selected profile. */
#define PWM_DRIVER_CONFIG_CHANNEL_COUNT 24u

/** @brief Number of logical channels owned by one physical bank. */
#define PWM_DRIVER_CONFIG_BANK_SIZE 8u

/** @brief Fixed physical bank; each bank owns 8 stable logical channels. */
typedef enum {
    PWM_DRIVER_CONFIG_BANK_A = 0, /**< HW-capable GPIOs, logical channels 0..7. */
    PWM_DRIVER_CONFIG_BANK_B, /**< PIO-capable GPIOs, logical channels 8..15. */
    PWM_DRIVER_CONFIG_BANK_C, /**< Software-only GPIOs, logical channels 16..23. */
    PWM_DRIVER_CONFIG_BANK_COUNT, /**< Number of physical banks. */
} pwm_driver_config_bank_t;

/** @brief Requested role when locking one bank. */
typedef enum {
    PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR = 0, /**< Bank channels become PWM outputs. */
    PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR, /**< Bank channels become PWM inputs. */
} pwm_driver_config_bank_role_t;

/** @brief Backend family selected for one fixed bank. */
typedef enum {
    PWM_DRIVER_CONFIG_BANK_BACKEND_HW = 0, /**< Bank A hardware PWM backend. */
    PWM_DRIVER_CONFIG_BANK_BACKEND_PIO, /**< Bank B PIO backend. */
    PWM_DRIVER_CONFIG_BANK_BACKEND_SW, /**< Software backend; valid for all banks, required for Bank C. */
} pwm_driver_config_bank_backend_t;

/** @brief Return the active profile's logical channel table. */
const pwm_driver_config_channel_t *pwm_driver_config_get_channel(uint channel);

/** @brief Resolve a backend-local channel to its profile-assigned GPIO. */
bool pwm_driver_config_get_gpio(pwm_driver_config_backend_t backend, uint backend_channel, uint *gpio_out);

/** @brief Return the logical channel for one backend-local channel. */
bool pwm_driver_config_get_logical_channel(pwm_driver_config_backend_t backend, uint backend_channel, uint *channel_out);

/** @brief Return the number of channels assigned to one backend. */
uint pwm_driver_config_backend_channel_count(pwm_driver_config_backend_t backend);

/** @brief Validate GPIO and backend-resource ownership for the selected profile. */
bool pwm_driver_config_validate(void);

/** @brief Validate an arbitrary profile table for host-side/profile tests. */
bool pwm_driver_config_validate_table(const pwm_driver_config_channel_t *channels, uint count);

/** @brief Return whether a profile channel accepts one requested frequency. */
bool pwm_driver_config_frequency_supported(uint channel, uint32_t frequency_hz);

/** @brief Return whether the selected profile is input-monitoring oriented. */
bool pwm_driver_config_is_monitor(void);

/** @brief Return a short name for one configured backend. */
const char *pwm_driver_config_backend_name(pwm_driver_config_backend_t backend);

#endif
