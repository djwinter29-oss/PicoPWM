/**
 * @file pwm_driver.c
 * @brief Core 0/Core 1 PWM wrapper, mailbox, and realized-state publication layer.
 */

#include "pwm_driver.h"
#include "pwm_driver_internal.h"

#include "hw_pwm_driver.h"
#include "pio/generator.h"
#include "sw_pwm_driver.h"

#include "hardware/clocks.h"
#include "pico/critical_section.h"
#include "pico/mutex.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "hardware/sync.h"

#include <math.h>

/** @brief Timeout for one admitted cross-core apply request in milliseconds. */
#define PWM_DRIVER_APPLY_TIMEOUT_MS 1000

/** @brief GPIO pin map for the hardware PWM logical channel bank. */
const uint PWM_HW_GPIO_PINS[HW_PWM_COUNT] = {
    1, 3, 5, 7, 9, 11, 13, 15
};

/** @brief GPIO pin map for the software PWM logical channel bank. */
const uint PWM_SW_GPIO_PINS[SW_PWM_COUNT] = {
    18, 19, 20, 21, 22, 25, 26, 27
};

/** @brief GPIO pin map for the PIO PWM logical channel bank. */
const uint PWM_PIO_GPIO_PINS[PIO_PWM_DRIVER_COUNT] = {
    0, 2, 4, 6, 8, 10, 12, 14
};

/** @brief Indicates whether Core 1 finished backend initialization. */
static volatile bool pwm_ready = false;

/** @brief One in-flight cross-core mailbox command record. */
typedef struct {
    uint8_t channel; /**< Logical channel index carried across the mailbox. */
    uint32_t freq_hz; /**< Requested frequency in Hz. */
    uint8_t duty; /**< Requested duty in percent in the range `[0, 100]`. */
} pwm_driver_cmd_t;

/** @brief Reply record published by Core 1 after one mailbox apply attempt. */
typedef struct {
    bool ok; /**< Indicates whether the backend accepted the command. */
} pwm_driver_reply_t;

/** @brief Shared realized-state snapshot record for one logical channel. */
typedef struct {
    volatile uint32_t version; /**< Even/odd version counter used for lock-free snapshot reads. */
    volatile uint32_t freq_hz; /**< Realized frequency in Hz. */
    volatile uint8_t duty; /**< Realized duty in percent in the range `[0, 100]`. */
    volatile uint32_t pulse_count; /**< Monotonic generated-period count from power-on. */
} pwm_driver_shared_state_t;

/** @brief Critical section protecting mailbox request and reply records. */
static critical_section_t pwm_reply_lock;
/** @brief Core 0 serialization lock shared by the public control entry points. */
static mutex_t control_api_lock;
/** @brief Published realized-state snapshot cache for all logical channels. */
static pwm_driver_shared_state_t pwm_state_cache[PWM_DRIVER_CHANNEL_COUNT];
/** @brief Indicates whether Core 0 has queued one pending mailbox command. */
static volatile bool pwm_cmd_pending = false;
/** @brief Indicates whether Core 1 is currently applying a claimed mailbox command. */
static volatile bool pwm_cmd_active = false;
/** @brief Indicates whether Core 1 published a reply for the last admitted command. */
static volatile bool pwm_reply_ready = false;
/** @brief Pending mailbox command record shared from Core 0 to Core 1. */
static pwm_driver_cmd_t pwm_pending_cmd = {0};
/** @brief Last mailbox reply record shared from Core 1 to Core 0. */
static pwm_driver_reply_t pwm_last_reply = {0};

/** @brief Return whether one logical channel belongs to the hardware PWM bank. */
static bool pwm_driver_is_hw_channel(uint channel) {
    return channel >= HW_PWM_CHANNEL_BASE && channel < PIO_PWM_CHANNEL_BASE;
}

/** @brief Return whether one logical channel belongs to the PIO PWM bank. */
static bool pwm_driver_is_pio_channel(uint channel) {
    return channel >= PIO_PWM_CHANNEL_BASE && channel < SW_PWM_CHANNEL_BASE;
}

/** @brief Return whether one logical channel belongs to the software PWM bank. */
static bool pwm_driver_is_sw_channel(uint channel) {
    return channel >= SW_PWM_CHANNEL_BASE && channel < PWM_DRIVER_CHANNEL_COUNT;
}

/**
 * @brief Submit one cross-core apply request while the public write lock is already held.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`.
 * @return Result code for the admitted command attempt.
 */
static pwm_driver_result_t pwm_driver_set_freq_locked(uint channel, uint32_t freq_hz, uint8_t duty);

/**
 * @brief Read one logical channel snapshot or return the shared default state when invalid.
 * @param channel Logical channel index.
 * @return Realized state when available, otherwise the shared default state.
 */
static pwm_driver_state_t control_get_state_or_default(uint channel) {
    pwm_driver_state_t state = {
        .freq_hz = 0u,
        .duty = 50u,
        .pulse_count = 0,
    };

    if (channel >= PWM_DRIVER_CHANNEL_COUNT) {
        return state;
    }

    pwm_driver_get(channel, &state);
    return state;
}

/**
 * @brief Check whether one requested frequency is supported by the logical channel class.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @return `true` when the request falls within the supported range for the channel backend.
 */
static bool freq_supported_for_channel(uint channel, uint32_t freq_hz) {
    if (freq_hz == 0u) {
        return true;
    }

    float requested_hz = (float)freq_hz;

    if (channel < PIO_PWM_CHANNEL_BASE) {
        const float sys_clk = (float)clock_get_hz(clk_sys);
        const float min_hw = sys_clk / ((255.0f + 15.0f / 16.0f) * 65536.0f);
        const float max_hw = sys_clk / 2.0f;
        return requested_hz >= min_hw && requested_hz <= max_hw;
    }

    if (channel < SW_PWM_CHANNEL_BASE) {
        return requested_hz <= 100000.0f;
    }

    return requested_hz <= 1000.0f;
}

/**
 * @brief Shared logical channel write helper used while the public write lock is already held.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`.
 * @return Result code from the cross-core apply path.
 */
static pwm_driver_result_t control_set_unlocked(uint channel, uint32_t freq_hz, uint8_t duty) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT) return PWM_DRIVER_RESULT_INVALID;
    if (!freq_supported_for_channel(channel, freq_hz)) return PWM_DRIVER_RESULT_INVALID;
    if (duty > 100u) duty = 100u;

    return pwm_driver_set_freq_locked(channel, freq_hz, duty);
}

/**
 * @brief Publish one full realized channel snapshot into the shared cache.
 * @param channel Logical channel index.
 * @param state Caller-owned realized channel snapshot.
 */
static void pwm_driver_cache_state(uint channel, const pwm_driver_state_t *state) {
    uint32_t irq_state;

    if (channel >= PWM_DRIVER_CHANNEL_COUNT || state == NULL) {
        return;
    }

    irq_state = save_and_disable_interrupts();
    pwm_state_cache[channel].version++;
    pwm_state_cache[channel].freq_hz = state->freq_hz;
    pwm_state_cache[channel].duty = state->duty;
    pwm_state_cache[channel].pulse_count = state->pulse_count;
    pwm_state_cache[channel].version++;
    restore_interrupts(irq_state);
}

/** @copydoc pwm_driver_store_applied_state */
void pwm_driver_store_applied_state(uint channel, const pwm_driver_state_t *state) {
    pwm_driver_cache_state(channel, state);
}

/** @copydoc pwm_driver_store_pulse_count */
void pwm_driver_store_pulse_count(uint channel, uint32_t pulse_count) {
    uint32_t irq_state;

    if (channel >= PWM_DRIVER_CHANNEL_COUNT) {
        return;
    }

    irq_state = save_and_disable_interrupts();
    pwm_state_cache[channel].version++;
    pwm_state_cache[channel].pulse_count = pulse_count;
    pwm_state_cache[channel].version++;
    restore_interrupts(irq_state);
}

/**
 * @brief Dispatch one logical channel write to the owning backend implementation.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in the normalized range `[0.0, 1.0]`.
 * @return `true` when the backend accepted the request.
 */
static bool pwm_driver_backend_set_freq(uint channel, float freq_hz, float duty) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT) {
        return false;
    }

    if (pwm_driver_is_hw_channel(channel)) {
        return hw_pwm_driver_set_freq(channel - HW_PWM_CHANNEL_BASE, freq_hz, duty);
    }

    if (pwm_driver_is_pio_channel(channel)) {
        return pio_pwm_generator_set_freq(channel - PIO_PWM_CHANNEL_BASE, freq_hz, duty);
    }

    return sw_pwm_driver_set_freq(channel - SW_PWM_CHANNEL_BASE, freq_hz, duty);
}

/**
 * @brief Dispatch one logical channel read to the owning backend implementation.
 * @param channel Logical channel index.
 * @param state Caller-owned destination for the realized state.
 * @return `true` when the backend returned a channel snapshot.
 */
static bool pwm_driver_backend_get(uint channel, pwm_driver_state_t *state) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT || state == NULL) {
        return false;
    }

    if (pwm_driver_is_hw_channel(channel)) {
        return hw_pwm_driver_get(channel - HW_PWM_CHANNEL_BASE, state);
    }

    if (pwm_driver_is_pio_channel(channel)) {
        return pio_pwm_generator_get(channel - PIO_PWM_CHANNEL_BASE, state);
    }

    return sw_pwm_driver_get(channel - SW_PWM_CHANNEL_BASE, state);
}

/** @brief Initialize the shared snapshot cache with the logical power-on default state. */
static void pwm_driver_cache_defaults(void) {
    for (uint channel = 0; channel < PWM_DRIVER_CHANNEL_COUNT; channel++) {
        pwm_driver_state_t state = {
            .freq_hz = 0u,
            .duty = 50u,
            .pulse_count = 0,
        };

        pwm_state_cache[channel].version = 0;
        pwm_driver_cache_state(channel, &state);
    }
}

/** @brief Claim and process all currently queued mailbox commands on Core 1. */
static void pwm_driver_process_mailbox(void) {
    pwm_driver_cmd_t cmd;
    bool has_cmd;

    do {
        critical_section_enter_blocking(&pwm_reply_lock);
        has_cmd = pwm_cmd_pending;
        if (has_cmd) {
            cmd = pwm_pending_cmd;
            pwm_cmd_pending = false;
            pwm_cmd_active = true;
        }
        critical_section_exit(&pwm_reply_lock);

        if (!has_cmd) {
            break;
        }

        bool ok = pwm_driver_backend_set_freq(
            cmd.channel,
            (float)cmd.freq_hz,
            pwm_driver_duty_ratio_from_percent(cmd.duty)
        );

        critical_section_enter_blocking(&pwm_reply_lock);
        pwm_last_reply.ok = ok;
        pwm_reply_ready = true;
        pwm_cmd_active = false;
        critical_section_exit(&pwm_reply_lock);
        __sev();
    } while (true);
}

/** @brief Core 1 main loop that owns backend initialization and mailbox processing. */
static void pwm_driver_core_main(void) {
    hw_pwm_driver_init();
    pio_pwm_generator_init();
    sw_pwm_driver_init();

    pwm_ready = true;

    while (true) {
        pwm_driver_process_mailbox();
        __wfe();
    }
}

/** @copydoc pwm_driver_launch */
void pwm_driver_launch(void) {
    pwm_ready = false;
    critical_section_init(&pwm_reply_lock);
    mutex_init(&control_api_lock);
    pwm_driver_cache_defaults();
    pwm_cmd_pending = false;
    pwm_cmd_active = false;
    pwm_reply_ready = false;
    pwm_pending_cmd = (pwm_driver_cmd_t){0};
    pwm_last_reply.ok = false;
    multicore_launch_core1(pwm_driver_core_main);
}

/** @copydoc pwm_driver_is_ready */
bool pwm_driver_is_ready(void) {
    return pwm_ready;
}

/** @copydoc pwm_driver_set_freq_locked */
pwm_driver_result_t pwm_driver_set_freq_locked(uint channel, uint32_t freq_hz, uint8_t duty) {
    absolute_time_t deadline;
    pwm_driver_reply_t reply;

    if (channel >= PWM_DRIVER_CHANNEL_COUNT) {
        return PWM_DRIVER_RESULT_INVALID;
    }

    if (get_core_num() != 0) {
        return PWM_DRIVER_RESULT_UNAVAILABLE;
    }

    if (!pwm_ready) {
        return PWM_DRIVER_RESULT_UNAVAILABLE;
    }

    if (duty > 100u) {
        duty = 100u;
    }

    pwm_driver_cmd_t cmd = {
        .channel = (uint8_t)channel,
        .freq_hz = freq_hz,
        .duty = duty,
    };

    critical_section_enter_blocking(&pwm_reply_lock);
    if (pwm_cmd_pending || pwm_cmd_active) {
        critical_section_exit(&pwm_reply_lock);
        return PWM_DRIVER_RESULT_BUSY;
    }

    pwm_pending_cmd = cmd;
    pwm_cmd_pending = true;
    pwm_reply_ready = false;
    critical_section_exit(&pwm_reply_lock);

    __sev();
    deadline = make_timeout_time_ms(PWM_DRIVER_APPLY_TIMEOUT_MS);

    do {
        critical_section_enter_blocking(&pwm_reply_lock);
        reply = pwm_last_reply;
        bool reply_ready = pwm_reply_ready;
        critical_section_exit(&pwm_reply_lock);
        if (reply_ready) {
            break;
        }

        if (time_reached(deadline)) {
            return PWM_DRIVER_RESULT_TIMEOUT;
        }

        tight_loop_contents();
    } while (true);

    return reply.ok ? PWM_DRIVER_RESULT_OK : PWM_DRIVER_RESULT_APPLY_FAILED;
}

/** @copydoc pwm_driver_set_freq */
pwm_driver_result_t pwm_driver_set_freq(uint channel, uint32_t freq_hz, uint8_t duty) {
    pwm_driver_result_t result;

    mutex_enter_blocking(&control_api_lock);
    result = pwm_driver_set_freq_locked(channel, freq_hz, duty);
    mutex_exit(&control_api_lock);

    return result;
}

/** @copydoc pwm_driver_get */
bool pwm_driver_get(uint channel, pwm_driver_state_t *state) {
    uint32_t version_before;
    uint32_t version_after;

    if (channel >= PWM_DRIVER_CHANNEL_COUNT || state == NULL) {
        return false;
    }

    if (get_core_num() != 0) {
        return pwm_driver_backend_get(channel, state);
    }

    do {
        version_before = pwm_state_cache[channel].version;
        if (version_before & 1u) {
            continue;
        }

        state->freq_hz = pwm_state_cache[channel].freq_hz;
        state->duty = pwm_state_cache[channel].duty;
        state->pulse_count = pwm_state_cache[channel].pulse_count;
        version_after = pwm_state_cache[channel].version;
    } while ((version_before != version_after) || (version_after & 1u));

    return true;
}

/** @copydoc control_set */
pwm_driver_result_t control_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    pwm_driver_result_t result;

    mutex_enter_blocking(&control_api_lock);
    result = control_set_unlocked(channel, freq_hz, duty);
    mutex_exit(&control_api_lock);

    return result;
}

/** @copydoc control_set_freq */
pwm_driver_result_t control_set_freq(uint channel, uint32_t freq_hz) {
    pwm_driver_result_t result;
    pwm_driver_state_t state;

    mutex_enter_blocking(&control_api_lock);
    state = control_get_state_or_default(channel);
    result = control_set_unlocked(channel, freq_hz, state.duty);
    mutex_exit(&control_api_lock);

    return result;
}

/** @copydoc control_set_duty */
pwm_driver_result_t control_set_duty(uint channel, uint8_t duty) {
    pwm_driver_result_t result;
    pwm_driver_state_t state;

    mutex_enter_blocking(&control_api_lock);
    state = control_get_state_or_default(channel);
    result = control_set_unlocked(channel, state.freq_hz, duty);
    mutex_exit(&control_api_lock);

    return result;
}

/** @copydoc control_get */
bool control_get(uint channel, pwm_driver_state_t *state) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT || state == NULL) {
        return false;
    }

    return pwm_driver_get(channel, state);
}

/** @copydoc control_get_freq */
uint32_t control_get_freq(uint channel) {
    return control_get_state_or_default(channel).freq_hz;
}

/** @copydoc control_get_duty */
uint8_t control_get_duty(uint channel) {
    return control_get_state_or_default(channel).duty;
}

/** @copydoc control_get_pulse_count */
uint32_t control_get_pulse_count(uint channel) {
    pwm_driver_state_t state = {0};

    if (channel >= PWM_DRIVER_CHANNEL_COUNT) return 0;
    if (!control_get(channel, &state)) return 0;

    return state.pulse_count;
}

/** @copydoc control_is_enabled */
bool control_is_enabled(uint channel) {
    return control_get_state_or_default(channel).freq_hz > 0u;
}

/** @copydoc control_stop_all */
pwm_driver_result_t control_stop_all(void) {
    pwm_driver_result_t status;

    mutex_enter_blocking(&control_api_lock);
    for (int i = 0; i < PWM_DRIVER_CHANNEL_COUNT; i++) {
        status = control_set_unlocked(i, 0u, 50u);
        if (status != PWM_DRIVER_RESULT_OK) {
            mutex_exit(&control_api_lock);
            return status;
        }
    }
    mutex_exit(&control_api_lock);

    return PWM_DRIVER_RESULT_OK;
}