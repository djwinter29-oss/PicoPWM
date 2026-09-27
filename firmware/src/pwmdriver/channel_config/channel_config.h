/**
 * @file channel_config.h
 * @brief Build-selected logical channel configuration for PicoPWM.
 */

#ifndef PWM_PROFILE_H
#define PWM_PROFILE_H

#include "pico/stdlib.h"

#include <stdbool.h>
#include <stdint.h>

#ifndef PWM_PROFILE_GPIO_COUNT
#define PWM_PROFILE_GPIO_COUNT 30u
#endif

#ifndef PWM_PROFILE_RESERVED_GPIO_MASK
#define PWM_PROFILE_RESERVED_GPIO_MASK ((1u << 26) | (1u << 27) | (1u << 25))
#endif

#ifndef PWM_PROFILE_REQUIRE_HW_CHANNEL_B
#define PWM_PROFILE_REQUIRE_HW_CHANNEL_B 1
#endif

/** @brief Backend implementation that owns one logical profile channel. */
typedef enum {
    PWM_PROFILE_BACKEND_HW_GENERATOR = 0, /**< Hardware PWM slice output, fixed to the 8 slice-B GPIOs. */
    PWM_PROFILE_BACKEND_PIO_GENERATOR, /**< PIO state-machine output, fixed to the 8 companion slice-A GPIOs. */
    PWM_PROFILE_BACKEND_SW_GENERATOR, /**< Software timer-driven output on any unclaimed GPIO. */
    PWM_PROFILE_BACKEND_HW_MONITOR, /**< Hardware-bank GPIO edge-timestamp input measurement. */
    PWM_PROFILE_BACKEND_PIO_MONITOR, /**< PIO-bank GPIO edge/DMA input measurement. */
    PWM_PROFILE_BACKEND_SW_MONITOR, /**< Software GPIO edge-timestamp input measurement on any unclaimed GPIO. */
} pwm_profile_backend_t;

/** @brief Signal direction for one logical profile channel. */
typedef enum {
    PWM_PROFILE_DIRECTION_OUTPUT = 0, /**< Channel generates a PWM output signal. */
    PWM_PROFILE_DIRECTION_INPUT, /**< Channel measures a PWM input signal. */
    PWM_PROFILE_DIRECTION_DISABLED, /**< Channel slot is unused by the selected profile. */
} pwm_profile_direction_t;

/** @brief Bitmask of operations one logical profile channel supports. */
typedef enum {
    PWM_PROFILE_CAP_READ = 1u << 0, /**< Channel state can be read back. */
    PWM_PROFILE_CAP_SET = 1u << 1, /**< Channel frequency/duty can be written. */
} pwm_profile_capability_t;

/** @brief Build-time descriptor for one logical PWM channel. */
typedef struct {
    pwm_profile_backend_t backend; /**< Backend implementation that owns this channel. */
    pwm_profile_direction_t direction; /**< Output, input, or disabled. */
    uint gpio; /**< Physical GPIO assigned to this channel; ignored when disabled. */
    uint backend_channel; /**< Backend-local index this channel maps to. */
    uint8_t capabilities; /**< Bitmask of `pwm_profile_capability_t` operations supported. */
    uint32_t min_frequency_hz; /**< Minimum generated or measurable nonzero frequency. */
    uint32_t max_frequency_hz; /**< Maximum generated or measurable frequency. */
    uint32_t accuracy_ppm; /**< Expected generation or measurement accuracy in parts-per-million. */
} pwm_profile_channel_t;

/** @brief Number of logical channels exposed by the selected profile. */
#define PWM_PROFILE_CHANNEL_COUNT 24u

/** @brief Number of logical channels owned by one physical bank. */
#define PWM_PROFILE_BANK_SIZE 8u

/** @brief Physical GPIO bank; each bank is a fixed set of 8 GPIOs (see docs/pinout.md). */
typedef enum {
    PWM_PROFILE_BANK_HW = 0, /**< Hardware PWM slice-B GPIOs, logical channels 0..7. */
    PWM_PROFILE_BANK_PIO, /**< PIO companion slice-A GPIOs, logical channels 8..15. */
    PWM_PROFILE_BANK_SW, /**< Software-only GPIOs, logical channels 16..23. */
    PWM_PROFILE_BANK_COUNT, /**< Number of physical banks. */
} pwm_profile_bank_t;

/** @brief Requested role when locking one bank. */
typedef enum {
    PWM_PROFILE_BANK_ROLE_GENERATOR = 0, /**< Bank channels become PWM outputs. */
    PWM_PROFILE_BANK_ROLE_MONITOR, /**< Bank channels become PWM inputs. */
} pwm_profile_bank_role_t;

/** @brief Return the active profile's logical channel table. */
const pwm_profile_channel_t *pwm_profile_get_channel(uint channel);

/** @brief Resolve a backend-local channel to its profile-assigned GPIO. */
bool pwm_profile_get_gpio(pwm_profile_backend_t backend, uint backend_channel, uint *gpio_out);

/** @brief Return the logical channel for one backend-local channel. */
bool pwm_profile_get_logical_channel(pwm_profile_backend_t backend, uint backend_channel, uint *channel_out);

/** @brief Return the number of channels assigned to one backend. */
uint pwm_profile_backend_channel_count(pwm_profile_backend_t backend);

/** @brief Validate GPIO and backend-resource ownership for the selected profile. */
bool pwm_profile_validate(void);

/** @brief Validate an arbitrary profile table for host-side/profile tests. */
bool pwm_profile_validate_table(const pwm_profile_channel_t *channels, uint count);

/** @brief Return whether a profile channel accepts one requested frequency. */
bool pwm_profile_frequency_supported(uint channel, uint32_t frequency_hz);

/** @brief Return whether the selected profile is input-monitoring oriented. */
bool pwm_profile_is_monitor(void);

/** @brief Return a short name for one configured backend. */
const char *pwm_profile_backend_name(pwm_profile_backend_t backend);

/** @brief Populate all fixed banks from startup roles before Core 1 launches. */
bool pwm_profile_configure_roles(const pwm_profile_bank_role_t roles[PWM_PROFILE_BANK_COUNT]);


#endif
