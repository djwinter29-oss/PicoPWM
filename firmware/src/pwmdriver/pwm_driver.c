/**
 * @file pwm_driver.c
 * @brief Core 0/Core 1 PWM wrapper, mailbox, and realized-state publication layer.
 */

#include "pwm_driver.h"
#include "pwm_driver_internal.h"

#include "hw/generator.h"
#include "pio/generator.h"
#include "sw/generator.h"

#include "pico/critical_section.h"
#include "pico/mutex.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "hardware/sync.h"

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

/** @brief Backend-local set operation signature used by the routing table. */
typedef bool (*pwm_driver_backend_set_fn_t)(uint channel, uint32_t freq_hz, uint8_t duty);
/** @brief Backend-native restore-defaults operation signature used by the routing table. */
typedef bool (*pwm_driver_backend_restore_defaults_fn_t)(void);
/** @brief Backend-owned readback finalization signature used by the routing table. */
typedef void (*pwm_driver_backend_finalize_readback_fn_t)(uint channel, pwm_driver_state_t *state, uint64_t pulse_ref_us);

/** @brief Mailbox operation kinds owned by the cross-core PWM wrapper. */
typedef enum {
    PWM_DRIVER_OP_SET_CHANNEL = 0, /**< Apply one logical channel update. */
    PWM_DRIVER_OP_RESTORE_DEFAULTS, /**< Restore all logical channels to their shared default state. */
} pwm_driver_op_t;

/** @brief Mailbox lifecycle states for the one-slot Core 0/Core 1 command exchange. */
typedef enum {
    PWM_DRIVER_MAILBOX_IDLE = 0, /**< No command is pending or waiting for collection. */
    PWM_DRIVER_MAILBOX_PENDING, /**< Core 0 published a command that Core 1 has not claimed yet. */
    PWM_DRIVER_MAILBOX_ACTIVE, /**< Core 1 claimed the command and is applying it. */
    PWM_DRIVER_MAILBOX_COMPLETE, /**< Core 1 published a reply for the last admitted command. */
} pwm_driver_mailbox_state_t;

/** @brief One in-flight cross-core mailbox command record. */
typedef struct {
    pwm_driver_op_t op; /**< Operation kind carried across the mailbox. */
    uint8_t channel; /**< Logical channel index carried across the mailbox. */
    uint32_t freq_hz; /**< Requested frequency in Hz. */
    uint8_t duty; /**< Requested duty in percent in the range `[0, 100]`. */
} pwm_driver_cmd_t;

/** @brief Reply record published by Core 1 after one mailbox apply attempt. */
typedef struct {
    bool ok; /**< Indicates whether the backend accepted the command. */
} pwm_driver_reply_t;

/** @brief One-slot Core 0/Core 1 mailbox state bundle. */
typedef struct {
    volatile pwm_driver_mailbox_state_t state; /**< Current lifecycle state of the mailbox slot. */
    pwm_driver_cmd_t cmd; /**< Last command published by Core 0 for Core 1 to claim. */
    pwm_driver_reply_t reply; /**< Last reply published by Core 1 for Core 0 to collect. */
} pwm_driver_mailbox_t;

/** @brief Routing descriptor for one logical backend bank. */
typedef struct {
    pwm_driver_backend_set_fn_t set; /**< Backend-local set callback. */
    pwm_driver_backend_restore_defaults_fn_t restore_defaults; /**< Backend-native restore-defaults callback. */
    pwm_driver_backend_finalize_readback_fn_t finalize_readback; /**< Optional backend-owned readback finalizer. */
} pwm_driver_backend_t;

/** @brief Shared realized-state snapshot record for one logical channel. */
typedef struct {
    volatile uint32_t version; /**< Even/odd version counter used for lock-free snapshot reads. */
    volatile uint32_t freq_hz; /**< Realized frequency in Hz. */
    volatile uint8_t duty; /**< Realized duty in percent in the range `[0, 100]`. */
    volatile uint32_t pulse_count; /**< Monotonic generated-period count from power-on. */
} pwm_driver_shared_state_t;

/** @brief Backend readback metadata paired with the shared snapshot cache. */
typedef struct {
    volatile uint64_t pulse_ref_us; /**< Cache timestamp paired with the published pulse counter. */
} pwm_driver_readback_t;

/** @brief Critical section protecting mailbox request and reply records. */
static critical_section_t pwm_reply_lock;
/** @brief Core 0 serialization lock shared by the public control entry points. */
static mutex_t control_api_lock;
/** @brief Published realized-state snapshot cache for all logical channels. */
static pwm_driver_shared_state_t pwm_state_cache[PWM_DRIVER_CHANNEL_COUNT];
/** @brief Per-channel readback metadata for backend-owned finalization work. */
static pwm_driver_readback_t pwm_readback[PWM_DRIVER_CHANNEL_COUNT];
/** @brief One-slot Core 0/Core 1 mailbox shared between submitter and backend owner. */
static pwm_driver_mailbox_t pwm_mailbox = {
    .state = PWM_DRIVER_MAILBOX_IDLE,
};

/** @brief Backend routing table in logical-channel order. */
static const pwm_driver_backend_t pwm_driver_backends[] = {
    {
        .set = hw_gen_set,
        .restore_defaults = hw_gen_restore_defaults,
        .finalize_readback = NULL,
    },
    {
        .set = pio_gen_set,
        .restore_defaults = pio_gen_restore_defaults,
        .finalize_readback = pio_gen_finalize_readback,
    },
    {
        .set = sw_gen_set,
        .restore_defaults = sw_gen_restore_defaults,
        .finalize_readback = NULL,
    },
};

/** @brief Classify one logical channel into its backend descriptor and backend-local channel index. */
static const pwm_driver_backend_t *pwm_driver_classify_channel(uint channel, uint *local_channel) {
    const pwm_driver_backend_t *backend;
    uint local;

    if (channel < PIO_PWM_CHANNEL_BASE) {
        backend = &pwm_driver_backends[0];
        local = channel;
    } else if (channel < SW_PWM_CHANNEL_BASE) {
        backend = &pwm_driver_backends[1];
        local = channel - PIO_PWM_CHANNEL_BASE;
    } else if (channel < PWM_DRIVER_CHANNEL_COUNT) {
        backend = &pwm_driver_backends[2];
        local = channel - SW_PWM_CHANNEL_BASE;
    } else {
        return NULL;
    }

    if (local_channel != NULL) {
        *local_channel = local;
    }

    return backend;
}

/**
 * @brief Submit one cross-core mailbox request while the public write lock is already held.
 * @param cmd Caller-owned mailbox request descriptor.
 * @return Result code for the admitted command attempt.
 */
static pwm_driver_result_t pwm_driver_submit_locked(const pwm_driver_cmd_t *cmd);

/**
 * @brief Publish one versioned snapshot update while the caller already owns the required coherence boundary.
 * @param channel Logical channel index.
 * @param freq_hz Optional realized frequency update; `NULL` keeps the current cached value.
 * @param duty Optional realized duty update; `NULL` keeps the current cached value.
 * @param pulse_count Pulse counter value to publish.
 * @param pulse_ref_us Timestamp paired with @p pulse_count.
 */
static void pwm_driver_cache_write_coherent(uint channel, const uint32_t *freq_hz, const uint8_t *duty, uint32_t pulse_count, uint64_t pulse_ref_us) {
    pwm_state_cache[channel].version++;
    if (freq_hz != NULL) {
        pwm_state_cache[channel].freq_hz = *freq_hz;
    }
    if (duty != NULL) {
        pwm_state_cache[channel].duty = *duty;
    }
    pwm_state_cache[channel].pulse_count = pulse_count;
    pwm_readback[channel].pulse_ref_us = pulse_ref_us;
    pwm_state_cache[channel].version++;
}

/**
 * @brief Publish one versioned snapshot update into the shared cache.
 * @param channel Logical channel index.
 * @param freq_hz Optional realized frequency update; `NULL` keeps the current cached value.
 * @param duty Optional realized duty update; `NULL` keeps the current cached value.
 * @param pulse_count Pulse counter value to publish.
 * @param pulse_ref_us Timestamp paired with @p pulse_count.
 */
static void pwm_driver_cache_write(uint channel, const uint32_t *freq_hz, const uint8_t *duty, uint32_t pulse_count, uint64_t pulse_ref_us) {
    uint32_t irq_state;

    if (channel >= PWM_DRIVER_CHANNEL_COUNT) {
        return;
    }

    irq_state = save_and_disable_interrupts();
    pwm_driver_cache_write_coherent(channel, freq_hz, duty, pulse_count, pulse_ref_us);
    restore_interrupts(irq_state);
}

/** @copydoc pwm_driver_store_applied_state */
void pwm_driver_store_applied_state(uint channel, const pwm_driver_state_t *state) {
    if (state == NULL) {
        return;
    }

    pwm_driver_cache_write(channel, &state->freq_hz, &state->duty, state->pulse_count, time_us_64());
}

/** @copydoc pwm_driver_store_applied_state_coherent */
void pwm_driver_store_applied_state_coherent(uint channel, const pwm_driver_state_t *state, uint64_t pulse_ref_us) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT || state == NULL) {
        return;
    }

    pwm_driver_cache_write_coherent(channel, &state->freq_hz, &state->duty, state->pulse_count, pulse_ref_us);
}

/** @copydoc pwm_driver_store_pulse_count */
void pwm_driver_store_pulse_count(uint channel, uint32_t pulse_count) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT) {
        return;
    }

    pwm_driver_cache_write(channel, NULL, NULL, pulse_count, time_us_64());
}

/**
 * @brief Dispatch one logical channel write to the owning backend implementation.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`.
 * @return `true` when the backend accepted the request.
 */
static bool pwm_driver_backend_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    const pwm_driver_backend_t *backend;
    uint local_channel;

    backend = pwm_driver_classify_channel(channel, &local_channel);
    if (backend == NULL || backend->set == NULL) {
        return false;
    }

    return backend->set(local_channel, freq_hz, duty);
}

/** @brief Apply the logical power-on defaults to all channels on Core 1. */
static bool pwm_driver_backend_restore_defaults(void) {
    for (uint i = 0; i < count_of(pwm_driver_backends); i++) {
        if (pwm_driver_backends[i].restore_defaults == NULL || !pwm_driver_backends[i].restore_defaults()) {
            return false;
        }
    }

    return true;
}

/** @brief Initialize the shared snapshot cache with the logical power-on default state. */
static void pwm_driver_cache_defaults(void) {
    for (uint channel = 0; channel < PWM_DRIVER_CHANNEL_COUNT; channel++) {
        pwm_driver_state_t state = {
            .freq_hz = 0u,
            .duty = 0u,
            .pulse_count = 0,
        };

        pwm_state_cache[channel].version = 0;
        pwm_driver_store_applied_state(channel, &state);
    }
}

/** @brief Claim and process all currently queued mailbox commands on Core 1. */
static void pwm_driver_process_mailbox(void) {
    pwm_driver_cmd_t cmd;
    bool has_cmd;

    do {
        critical_section_enter_blocking(&pwm_reply_lock);
        has_cmd = pwm_mailbox.state == PWM_DRIVER_MAILBOX_PENDING;
        if (has_cmd) {
            cmd = pwm_mailbox.cmd;
            pwm_mailbox.state = PWM_DRIVER_MAILBOX_ACTIVE;
        }
        critical_section_exit(&pwm_reply_lock);

        if (!has_cmd) {
            break;
        }

        bool ok = false;
        if (cmd.op == PWM_DRIVER_OP_SET_CHANNEL) {
            ok = pwm_driver_backend_set(
                cmd.channel,
                cmd.freq_hz,
                cmd.duty
            );
        } else if (cmd.op == PWM_DRIVER_OP_RESTORE_DEFAULTS) {
            ok = pwm_driver_backend_restore_defaults();
        }

        critical_section_enter_blocking(&pwm_reply_lock);
        pwm_mailbox.reply.ok = ok;
        pwm_mailbox.state = PWM_DRIVER_MAILBOX_COMPLETE;
        critical_section_exit(&pwm_reply_lock);
        __sev();
    } while (true);
}

/** @brief Core 1 main loop that owns backend initialization and mailbox processing. */
static void pwm_driver_core_main(void) {
    hw_gen_init();
    pio_gen_init();
    sw_gen_init();

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
    pwm_mailbox.state = PWM_DRIVER_MAILBOX_IDLE;
    pwm_mailbox.cmd = (pwm_driver_cmd_t){0};
    pwm_mailbox.reply.ok = false;
    multicore_launch_core1(pwm_driver_core_main);
}

/** @copydoc pwm_driver_is_ready */
bool pwm_driver_is_ready(void) {
    return pwm_ready;
}

/** @copydoc pwm_driver_submit_locked */
pwm_driver_result_t pwm_driver_submit_locked(const pwm_driver_cmd_t *cmd) {
    absolute_time_t deadline;
    pwm_driver_reply_t reply;

    if (get_core_num() != 0) {
        return PWM_DRIVER_RESULT_UNAVAILABLE;
    }

    if (!pwm_ready) {
        return PWM_DRIVER_RESULT_UNAVAILABLE;
    }

    if (cmd == NULL) {
        return PWM_DRIVER_RESULT_INVALID;
    }

    if (cmd->op != PWM_DRIVER_OP_SET_CHANNEL && cmd->op != PWM_DRIVER_OP_RESTORE_DEFAULTS) {
        return PWM_DRIVER_RESULT_INVALID;
    }

    if (cmd->op == PWM_DRIVER_OP_SET_CHANNEL && cmd->channel >= PWM_DRIVER_CHANNEL_COUNT) {
        return PWM_DRIVER_RESULT_INVALID;
    }

    critical_section_enter_blocking(&pwm_reply_lock);
    if (pwm_mailbox.state == PWM_DRIVER_MAILBOX_PENDING || pwm_mailbox.state == PWM_DRIVER_MAILBOX_ACTIVE) {
        critical_section_exit(&pwm_reply_lock);
        return PWM_DRIVER_RESULT_BUSY;
    }

    pwm_mailbox.cmd = *cmd;
    pwm_mailbox.state = PWM_DRIVER_MAILBOX_PENDING;
    critical_section_exit(&pwm_reply_lock);

    __sev();
    deadline = make_timeout_time_ms(PWM_DRIVER_APPLY_TIMEOUT_MS);

    do {
        critical_section_enter_blocking(&pwm_reply_lock);
        reply = pwm_mailbox.reply;
        bool reply_ready = pwm_mailbox.state == PWM_DRIVER_MAILBOX_COMPLETE;
        if (reply_ready) {
            pwm_mailbox.state = PWM_DRIVER_MAILBOX_IDLE;
        }
        critical_section_exit(&pwm_reply_lock);
        if (reply_ready) {
            break;
        }

        if (time_reached(deadline)) {
            return PWM_DRIVER_RESULT_TIMEOUT;
        }

        best_effort_wfe_or_timeout(deadline);
    } while (true);

    return reply.ok ? PWM_DRIVER_RESULT_OK : PWM_DRIVER_RESULT_APPLY_FAILED;
}

/** @copydoc pwm_driver_set */
pwm_driver_result_t pwm_driver_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    pwm_driver_result_t result;

    if (channel >= PWM_DRIVER_CHANNEL_COUNT) {
        return PWM_DRIVER_RESULT_INVALID;
    }
    if (duty > 100u) {
        duty = 100u;
    }

    mutex_enter_blocking(&control_api_lock);
    result = pwm_driver_submit_locked(&(pwm_driver_cmd_t) {
        .op = PWM_DRIVER_OP_SET_CHANNEL,
        .channel = (uint8_t)channel,
        .freq_hz = freq_hz,
        .duty = duty,
    });
    mutex_exit(&control_api_lock);

    return result;
}

/** @copydoc pwm_driver_get */
bool pwm_driver_get(uint channel, pwm_driver_state_t *state) {
    const pwm_driver_backend_t *backend;
    uint local_channel;
    uint32_t version_before;
    uint32_t version_after;
    uint64_t pulse_ref_us;

    if (channel >= PWM_DRIVER_CHANNEL_COUNT || state == NULL) {
        return false;
    }

    backend = pwm_driver_classify_channel(channel, &local_channel);
    if (backend == NULL) {
        return false;
    }

    do {
        version_before = pwm_state_cache[channel].version;
        if (version_before & 1u) {
            continue;
        }

        state->freq_hz = pwm_state_cache[channel].freq_hz;
        state->duty = pwm_state_cache[channel].duty;
        state->pulse_count = pwm_state_cache[channel].pulse_count;
        pulse_ref_us = pwm_readback[channel].pulse_ref_us;
        version_after = pwm_state_cache[channel].version;
    } while ((version_before != version_after) || (version_after & 1u));

    if (backend->finalize_readback != NULL) {
        backend->finalize_readback(local_channel, state, pulse_ref_us);
    }

    return true;
}

/** @copydoc pwm_driver_restore_defaults */
pwm_driver_result_t pwm_driver_restore_defaults(void) {
    pwm_driver_result_t status;

    mutex_enter_blocking(&control_api_lock);
    status = pwm_driver_submit_locked(&(pwm_driver_cmd_t) {
        .op = PWM_DRIVER_OP_RESTORE_DEFAULTS,
    });
    mutex_exit(&control_api_lock);

    return status;
}