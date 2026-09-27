/**
 * @file pwm_driver.c
 * @brief Core 0/Core 1 PWM wrapper, mailbox, and realized-state publication layer.
 */

#include "pwm_driver.h"
#include "pwm_driver_internal.h"
#include "pwm_driver_mailbox.h"

#include "board/system.h"

#include "monitor/software_monitor.h"
#include "generator/pio_generator.h"
#include "generator/software_generator.h"
#include "monitor/hardware_monitor.h"
#include "monitor/pio_monitor.h"
#include "generator/hardware_generator.h"

#include "pico/critical_section.h"
#include "pico/mutex.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "hardware/sync.h"

#include "hardware/gpio.h"

static inline void pwm_driver_memory_barrier(void) {
#if defined(__arm__) || defined(__thumb__)
    __asm volatile("dmb ish" ::: "memory");
#else
    __sync_synchronize();
#endif
}

/** @brief Timeout for one admitted cross-core apply request in milliseconds. */
#define PWM_DRIVER_APPLY_TIMEOUT_MS 1000

uint pwm_driver_get_gpio(pwm_driver_config_backend_t backend, uint backend_channel) {
    uint gpio = 0u;

    hard_assert(pwm_driver_config_get_gpio(backend, backend_channel, &gpio));
    return gpio;
}

uint pwm_driver_get_logical_channel(pwm_driver_config_backend_t backend, uint backend_channel) {
    uint channel = 0u;

    hard_assert(pwm_driver_config_get_logical_channel(backend, backend_channel, &channel));
    return channel;
}

/** @brief Indicates whether Core 1 finished backend initialization. */
typedef enum {
    PWM_DRIVER_LIFECYCLE_RESET = 0,
    PWM_DRIVER_LIFECYCLE_STARTING,
    PWM_DRIVER_LIFECYCLE_READY,
    PWM_DRIVER_LIFECYCLE_FAILED,
} pwm_driver_lifecycle_t;

static volatile pwm_driver_lifecycle_t pwm_lifecycle = PWM_DRIVER_LIFECYCLE_RESET;

/** @brief Backend-local set operation signature used by the routing table. */
typedef bool (*pwm_driver_backend_set_fn_t)(uint channel, uint32_t freq_hz, uint8_t duty);
/** @brief Backend-native restore-defaults operation signature used by the routing table. */
typedef bool (*pwm_driver_backend_restore_defaults_fn_t)(void);
/** @brief Backend initialization operation signature used by the routing table. */
typedef bool (*pwm_driver_backend_init_fn_t)(void);
/** @brief Read one backend-local monitor sample. */
typedef bool (*pwm_driver_backend_get_fn_t)(uint channel, pwm_driver_state_t *state);
/** @brief Backend-owned readback finalization signature used by the routing table. */
typedef void (*pwm_driver_backend_finalize_readback_fn_t)(uint channel, pwm_driver_state_t *state,
                                                          uint64_t pulse_ref_us);

/** @brief Mailbox operation kinds owned by the cross-core PWM wrapper. */
typedef enum {
    PWM_DRIVER_OP_SET_CHANNEL = 0,  /**< Apply one logical channel update. */
    PWM_DRIVER_OP_RESTORE_DEFAULTS, /**< Restore all logical channels to their shared default state. */
} pwm_driver_op_t;

/** @brief One in-flight cross-core mailbox command record. */
typedef struct {
    pwm_driver_op_t op; /**< Operation kind carried across the mailbox. */
    uint8_t channel;    /**< Logical channel index for `SET_CHANNEL`. */
    uint32_t freq_hz;   /**< Requested frequency in Hz. */
    uint8_t duty;       /**< Requested duty for `SET_CHANNEL`. */
} pwm_driver_cmd_t;

/** @brief Reply record published by Core 1 after one mailbox apply attempt. */
typedef struct {
    bool ok; /**< Indicates whether the backend accepted the command. */
} pwm_driver_reply_t;

/** @brief One-slot Core 0/Core 1 mailbox state bundle. */
typedef struct {
    volatile pwm_driver_mailbox_state_t state; /**< Current lifecycle state of the mailbox slot. */
    pwm_driver_cmd_t cmd;                      /**< Last command published by Core 0 for Core 1 to claim. */
    pwm_driver_reply_t reply;                  /**< Last reply published by Core 1 for Core 0 to collect. */
} pwm_driver_mailbox_t;

/** @brief Routing descriptor for one logical backend bank. */
typedef struct {
    pwm_driver_backend_init_fn_t init;                           /**< Backend initialization callback. */
    pwm_driver_backend_set_fn_t set;                             /**< Backend-local set callback. */
    pwm_driver_backend_restore_defaults_fn_t restore_defaults;   /**< Backend-native restore-defaults callback. */
    pwm_driver_backend_get_fn_t get;                             /**< Optional backend-local read callback. */
    pwm_driver_backend_finalize_readback_fn_t finalize_readback; /**< Optional backend-owned readback finalizer. */
} pwm_driver_backend_t;

/** @brief Shared realized-state snapshot record for one logical channel. */
typedef struct {
    volatile uint32_t version;     /**< Even/odd version counter used for lock-free snapshot reads. */
    volatile uint32_t freq_hz;     /**< Realized frequency in Hz. */
    volatile uint8_t duty;         /**< Realized duty in percent in the range `[0, 100]`. */
    volatile uint32_t pulse_count; /**< Monotonic generated-period count from power-on. */
} pwm_driver_shared_state_t;

/** @brief Backend readback metadata paired with the shared snapshot cache. */
typedef struct {
    volatile uint64_t pulse_ref_us; /**< Cache timestamp paired with the published pulse counter. */
} pwm_driver_readback_t;

/** @brief Core 1 loop counter used to detect a stalled backend owner. */
static volatile uint32_t pwm_core1_heartbeat;
/** @brief Last Core 1 heartbeat observed by a Core 0 mailbox wait. */
static uint32_t pwm_core1_heartbeat_seen;
/** @brief True after Core 0 has sampled the Core 1 heartbeat once. */
static bool pwm_core1_heartbeat_seen_valid;
/** @brief Time of the last observed Core 1 heartbeat change. */
static absolute_time_t pwm_core1_heartbeat_stamp;
/** @brief Optional Core 0 callback invoked while a mailbox wait is spinning. */
static void (*pwm_wait_hook)(void);
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
static const pwm_driver_backend_t pwm_driver_backends[PWM_DRIVER_CONFIG_BACKEND_SW_MONITOR + 1u] = {
    [PWM_DRIVER_CONFIG_BACKEND_HW_MONITOR] =
        {
            .init = hw_mon_init,
            .set = NULL,
            .restore_defaults = NULL,
            .get = hw_mon_get,
            .finalize_readback = NULL,
        },
    [PWM_DRIVER_CONFIG_BACKEND_PIO_MONITOR] =
        {
            .init = pio_mon_init,
            .set = NULL,
            .restore_defaults = NULL,
            .get = pio_mon_get,
            .finalize_readback = NULL,
        },
    [PWM_DRIVER_CONFIG_BACKEND_SW_MONITOR] =
        {
            .init = sw_mon_init,
            .set = NULL,
            .restore_defaults = NULL,
            .get = sw_mon_get,
            .finalize_readback = NULL,
        },
    [PWM_DRIVER_CONFIG_BACKEND_SW_GENERATOR] =
        {
            .init = sw_gen_init,
            .set = sw_gen_set,
            .restore_defaults = sw_gen_restore_defaults,
            .get = NULL,
            .finalize_readback = NULL,
        },
    [PWM_DRIVER_CONFIG_BACKEND_HW_GENERATOR] =
        {
            .init = hw_gen_init,
            .set = hw_gen_set,
            .restore_defaults = hw_gen_restore_defaults,
            .get = NULL,
            .finalize_readback = NULL,
        },
    [PWM_DRIVER_CONFIG_BACKEND_PIO_GENERATOR] =
        {
            .init = pio_gen_init,
            .set = pio_gen_set,
            .restore_defaults = pio_gen_restore_defaults,
            .get = NULL,
            .finalize_readback = pio_gen_finalize_readback,
        },
};

/** @brief Dispatch the Pico SDK's single GPIO callback to all configured monitor banks. */
static void pwm_driver_monitor_gpio_irq(uint gpio, uint32_t events) {
    hw_mon_handle_gpio_irq(gpio, events);
    sw_mon_handle_gpio_irq(gpio, events);
}

/** @brief Resolve one configured logical channel into its backend descriptor. */
static const pwm_driver_backend_t *pwm_driver_get_backend(uint channel, uint *local_channel) {
    const pwm_driver_config_channel_t *profile;
    const pwm_driver_backend_t *backend;

    profile = pwm_driver_config_get_channel(channel);
    if (profile == NULL) {
        return NULL;
    }

    if (local_channel != NULL) {
        *local_channel = profile->backend_channel;
    }

    if (profile->backend > PWM_DRIVER_CONFIG_BACKEND_SW_MONITOR) {
        return NULL;
    }

    backend = &pwm_driver_backends[profile->backend];

    return backend;
}

/** @brief Resolve one configured output channel for a write operation. */
static const pwm_driver_backend_t *pwm_driver_classify_channel(uint channel, uint *local_channel) {
    const pwm_driver_config_channel_t *profile = pwm_driver_config_get_channel(channel);

    if ((profile == NULL) || ((profile->capabilities & PWM_DRIVER_CONFIG_CAP_SET) == 0u) ||
        profile->direction != PWM_DRIVER_CONFIG_DIRECTION_OUTPUT) {
        return NULL;
    }

    return pwm_driver_get_backend(channel, local_channel);
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
static void pwm_driver_cache_write_coherent(uint channel, const uint32_t *freq_hz, const uint8_t *duty,
                                            uint32_t pulse_count, uint64_t pulse_ref_us) {
    pwm_state_cache[channel].version++;
    if (freq_hz != NULL) {
        pwm_state_cache[channel].freq_hz = *freq_hz;
    }
    if (duty != NULL) {
        pwm_state_cache[channel].duty = *duty;
    }
    pwm_state_cache[channel].pulse_count = pulse_count;
    pwm_readback[channel].pulse_ref_us = pulse_ref_us;
    pwm_driver_memory_barrier();
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
static void pwm_driver_cache_write(uint channel, const uint32_t *freq_hz, const uint8_t *duty, uint32_t pulse_count,
                                   uint64_t pulse_ref_us) {
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
        if (pwm_driver_config_backend_channel_count((pwm_driver_config_backend_t)i) == 0u) {
            continue;
        }
        if ((pwm_driver_backends[i].restore_defaults != NULL) && !pwm_driver_backends[i].restore_defaults()) {
            return false;
        }
    }

    return true;
}

/** @brief Initialize every backend selected by the already-populated startup table. */
static bool pwm_driver_initialize_backends(void) {
    for (uint i = 0u; i < count_of(pwm_driver_backends); ++i) {
        if ((pwm_driver_config_backend_channel_count((pwm_driver_config_backend_t)i) != 0u) &&
            ((pwm_driver_backends[i].init == NULL) || !pwm_driver_backends[i].init())) {
            return false;
        }
    }

    return true;
}

/** @brief Publish fresh monitor samples from Core 1 into the shared state cache. */
static void pwm_driver_refresh_monitor_state(void) {
    for (uint channel = 0u; channel < PWM_DRIVER_CHANNEL_COUNT; ++channel) {
        const pwm_driver_config_channel_t *profile = pwm_driver_config_get_channel(channel);
        const pwm_driver_backend_t *backend;
        pwm_driver_state_t state;
        uint local_channel;

        if ((profile == NULL) || ((profile->capabilities & PWM_DRIVER_CONFIG_CAP_READ) == 0u) ||
            profile->direction != PWM_DRIVER_CONFIG_DIRECTION_INPUT) {
            continue;
        }

        backend = pwm_driver_get_backend(channel, &local_channel);
        if ((backend != NULL) && (backend->get != NULL) && backend->get(local_channel, &state)) {
            pwm_driver_store_applied_state(channel, &state);
        }
    }
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
            ok = pwm_driver_backend_set(cmd.channel, cmd.freq_hz, cmd.duty);
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

/** @brief Return false when Core 1 has not advanced its loop counter within the apply timeout. */
static bool pwm_driver_core1_heartbeat_ok(void) {
    uint32_t heartbeat;

    pwm_driver_memory_barrier();
    heartbeat = pwm_core1_heartbeat;
    if (!pwm_core1_heartbeat_seen_valid || heartbeat != pwm_core1_heartbeat_seen) {
        pwm_core1_heartbeat_seen = heartbeat;
        pwm_core1_heartbeat_seen_valid = true;
        pwm_core1_heartbeat_stamp = get_absolute_time();
        return true;
    }

    return absolute_time_diff_us(pwm_core1_heartbeat_stamp, get_absolute_time()) <
           (int64_t)PWM_DRIVER_APPLY_TIMEOUT_MS * 1000;
}

/** @brief Core 1 main loop that owns backend initialization and mailbox processing. */
static void pwm_driver_core_main(void) {
    /* Required before Core 0 may call multicore_lockout_start_blocking() from config save. */
    multicore_lockout_victim_init();

    /* Register the shared GPIO callback before backend initialization arms monitor pins. */
    gpio_set_irq_callback(pwm_driver_monitor_gpio_irq);

    if (!pwm_driver_initialize_backends()) {
        pwm_lifecycle = PWM_DRIVER_LIFECYCLE_FAILED;
        return;
    }

    pwm_lifecycle = PWM_DRIVER_LIFECYCLE_READY;

    while (true) {
        pwm_core1_heartbeat++;
        pwm_driver_memory_barrier();
        pwm_driver_process_mailbox();
        pwm_driver_refresh_monitor_state();
        best_effort_wfe_or_timeout(make_timeout_time_us(1000));
    }
}

void pwm_driver_set_wait_hook(void (*hook)(void)) {
    pwm_wait_hook = hook;
}

bool pwm_driver_init(const pwm_driver_config_t *config) {
    pwm_driver_config_bank_role_t roles[PWM_DRIVER_CONFIG_BANK_COUNT];
    pwm_driver_config_bank_backend_t backends[PWM_DRIVER_CONFIG_BANK_COUNT];

    if ((config == NULL) || (pwm_lifecycle != PWM_DRIVER_LIFECYCLE_RESET)) {
        return false;
    }

    if (!pwm_driver_config_validate_target(config)) {
        return false;
    }

    backends[PWM_DRIVER_CONFIG_BANK_A] = config->bank_a_backend;
    backends[PWM_DRIVER_CONFIG_BANK_B] = config->bank_b_backend;
    backends[PWM_DRIVER_CONFIG_BANK_C] = config->bank_c_backend;
    roles[PWM_DRIVER_CONFIG_BANK_A] = config->bank_a_role;
    roles[PWM_DRIVER_CONFIG_BANK_B] = config->bank_b_role;
    roles[PWM_DRIVER_CONFIG_BANK_C] = config->bank_c_role;
    if (!pwm_driver_configure_table(backends, roles)) {
        return false;
    }

    pwm_lifecycle = PWM_DRIVER_LIFECYCLE_STARTING;
    critical_section_init(&pwm_reply_lock);
    mutex_init(&control_api_lock);
    pwm_driver_cache_defaults();
    pwm_mailbox.state = PWM_DRIVER_MAILBOX_IDLE;
    pwm_mailbox.cmd = (pwm_driver_cmd_t){0};
    pwm_mailbox.reply.ok = false;
    multicore_launch_core1(pwm_driver_core_main);
    return true;
}

/** @copydoc pwm_driver_is_ready */
bool pwm_driver_is_ready(void) {
    return pwm_lifecycle == PWM_DRIVER_LIFECYCLE_READY;
}

bool pwm_driver_startup_failed(void) {
    return pwm_lifecycle == PWM_DRIVER_LIFECYCLE_FAILED;
}

/** @copydoc pwm_driver_submit_locked */
pwm_driver_result_t pwm_driver_submit_locked(const pwm_driver_cmd_t *cmd) {
    absolute_time_t deadline;
    pwm_driver_reply_t reply;

    if (get_core_num() != 0) {
        return PWM_DRIVER_RESULT_UNAVAILABLE;
    }

    if (pwm_lifecycle != PWM_DRIVER_LIFECYCLE_READY) {
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
    if (!pwm_mailbox_admits_submit(pwm_mailbox.state)) {
        critical_section_exit(&pwm_reply_lock);
        return PWM_DRIVER_RESULT_BUSY;
    }

    /* A completed reply left by a previous waiter is stale once a new command is admitted. */
    pwm_mailbox.state = PWM_DRIVER_MAILBOX_IDLE;
    pwm_mailbox.cmd = *cmd;
    pwm_mailbox.state = PWM_DRIVER_MAILBOX_PENDING;
    critical_section_exit(&pwm_reply_lock);

    __sev();
    deadline = make_timeout_time_ms(PWM_DRIVER_APPLY_TIMEOUT_MS);
    pwm_driver_memory_barrier();
    pwm_core1_heartbeat_seen = pwm_core1_heartbeat;
    pwm_core1_heartbeat_seen_valid = true;
    pwm_core1_heartbeat_stamp = get_absolute_time();

    do {
        bool timed_out = false;
        bool cancelled = false;
        pwm_mailbox_timeout_action_t action = PWM_MAILBOX_TIMEOUT_WAIT;

        critical_section_enter_blocking(&pwm_reply_lock);
        if (pwm_mailbox.state == PWM_DRIVER_MAILBOX_COMPLETE) {
            reply = pwm_mailbox.reply;
            pwm_mailbox.state = PWM_DRIVER_MAILBOX_IDLE;
            critical_section_exit(&pwm_reply_lock);
            return reply.ok ? PWM_DRIVER_RESULT_OK : PWM_DRIVER_RESULT_APPLY_FAILED;
        }

        timed_out = time_reached(deadline);
        if (timed_out) {
            action = pwm_mailbox_timeout_action(pwm_mailbox.state);
            if (action == PWM_MAILBOX_TIMEOUT_CANCEL) {
                pwm_mailbox.state = PWM_DRIVER_MAILBOX_IDLE;
                cancelled = true;
            }
        }
        critical_section_exit(&pwm_reply_lock);

        /*
         * A live Core 1 loop heartbeats about once per millisecond. No progress
         * across the apply deadline means the backend owner is stuck.
         */
        if (timed_out && !pwm_driver_core1_heartbeat_ok()) {
            system_reboot();
        }
        if (cancelled) {
            return PWM_DRIVER_RESULT_TIMEOUT;
        }

        system_watchdog_kick();
        if (pwm_wait_hook != NULL) {
            pwm_wait_hook();
        }
        best_effort_wfe_or_timeout(make_timeout_time_us(1000));
    } while (true);
}

/** @copydoc pwm_driver_set */
pwm_driver_result_t pwm_driver_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    const pwm_driver_config_channel_t *profile;
    pwm_driver_result_t result;

    profile = pwm_driver_config_get_channel(channel);
    if (profile == NULL) {
        return PWM_DRIVER_RESULT_INVALID;
    }
    if ((profile->capabilities & PWM_DRIVER_CONFIG_CAP_SET) == 0u ||
        profile->direction != PWM_DRIVER_CONFIG_DIRECTION_OUTPUT) {
        return PWM_DRIVER_RESULT_UNAVAILABLE;
    }
    if (!pwm_driver_config_frequency_supported(channel, freq_hz)) {
        return PWM_DRIVER_RESULT_INVALID;
    }
    if (duty > 100u) {
        duty = 100u;
    }

    mutex_enter_blocking(&control_api_lock);
    result = pwm_driver_submit_locked(&(pwm_driver_cmd_t){
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
    const pwm_driver_config_channel_t *profile;
    const pwm_driver_backend_t *backend;
    uint local_channel;
    uint32_t version_before;
    uint32_t version_after;
    uint64_t pulse_ref_us;

    profile = pwm_driver_config_get_channel(channel);
    if ((profile == NULL) || (state == NULL) || ((profile->capabilities & PWM_DRIVER_CONFIG_CAP_READ) == 0u)) {
        return false;
    }

    backend = pwm_driver_get_backend(channel, &local_channel);
    if (backend == NULL) {
        return false;
    }

    do {
        version_before = pwm_state_cache[channel].version;
        if (version_before & 1u) {
            continue;
        }

        pwm_driver_memory_barrier();
        state->freq_hz = pwm_state_cache[channel].freq_hz;
        state->duty = pwm_state_cache[channel].duty;
        state->pulse_count = pwm_state_cache[channel].pulse_count;
        pulse_ref_us = pwm_readback[channel].pulse_ref_us;
        pwm_driver_memory_barrier();
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
    status = pwm_driver_submit_locked(&(pwm_driver_cmd_t){
        .op = PWM_DRIVER_OP_RESTORE_DEFAULTS,
    });
    mutex_exit(&control_api_lock);

    return status;
}
