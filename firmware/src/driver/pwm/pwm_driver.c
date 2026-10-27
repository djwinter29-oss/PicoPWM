/**
 * @file pwm_driver.c
 * @brief Core 0/Core 1 PWM wrapper, mailbox, and realized-state publication layer.
 */

#include "pwm_driver.h"
#include "pwm_driver_internal.h"

#include "platform/system.h"

#include "software/software_monitor.h"
#include "pio/pio_generator.h"
#include "software/software_generator.h"
#include "hardware/hardware_monitor.h"
#include "pio/pio_monitor.h"
#include "hardware/hardware_generator.h"

#include "pico/critical_section.h"
#include "pico/mutex.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "hardware/sync.h"

#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include <string.h>

/** @brief Bank roles chosen at startup; set once before Core 1 launches. */
static pwm_role_t pwm_driver_config_bank_roles[PWM_BANK_COUNT];
static bool pwm_driver_config_table_configured = false;

/** @brief Return the board GPIO for one channel of a bank. */
static uint pwm_driver_config_bank_gpio(pwm_bank_t bank, uint index) {
    switch (bank) {
    case PWM_BANK_A:
        return hardware_pwm_config.channels[index].gpio;
    case PWM_BANK_B:
        return pio_pwm_config.channels[index].gpio;
    default:
        return software_pwm_config.channels[index].gpio;
    }
}

bool pwm_driver_config_channel_role(uint channel, pwm_role_t *role) {
    if (!pwm_driver_config_table_configured || (role == NULL) || (channel >= PWM_DRIVER_CONFIG_CHANNEL_COUNT)) {
        return false;
    }
    *role = pwm_driver_config_bank_roles[channel / PWM_DRIVER_CONFIG_BANK_SIZE];
    return true;
}

const char *pwm_driver_config_channel_name(uint channel) {
    static const char *const names[PWM_BANK_COUNT][2] = {
        {"HW", "HW-MON"},
        {"PIO", "PIO-MON"},
        {"SW", "SW-MON"},
    };
    pwm_role_t role;

    return pwm_driver_config_channel_role(channel, &role) ? names[channel / PWM_DRIVER_CONFIG_BANK_SIZE][role] : "?";
}

bool pwm_driver_config_validate(void) {
    uint32_t used = 0u;

    for (pwm_bank_t bank = PWM_BANK_A; bank < PWM_BANK_COUNT; ++bank) {
        for (uint i = 0u; i < PWM_DRIVER_CONFIG_BANK_SIZE; ++i) {
            uint gpio = pwm_driver_config_bank_gpio(bank, i);

            if (gpio >= PWM_DRIVER_CONFIG_GPIO_COUNT || (used & (1u << gpio)) != 0u) {
                return false;
            }
            used |= 1u << gpio;

            if ((gpio == 23u || gpio == 24u) && bank != PWM_BANK_C) {
                return false;
            }
            if (PWM_DRIVER_CONFIG_REQUIRE_HW_CHANNEL_B && bank == PWM_BANK_A &&
                pwm_gpio_to_channel(gpio) != PWM_CHAN_B) {
                return false;
            }
        }
    }

    return true;
}

bool pwm_driver_config_frequency_supported(uint channel, uint32_t frequency_hz) {
    pwm_role_t role;
    uint32_t min_hz = 1u;
    uint32_t max_hz = 1000000u;

    if (!pwm_driver_config_channel_role(channel, &role)) {
        return false;
    }
    if (frequency_hz == 0u) {
        return true;
    }

    if (channel / PWM_DRIVER_CONFIG_BANK_SIZE == PWM_BANK_A && role == PWM_ROLE_GENERATOR) {
        min_hz = 10u;
    } else if (channel / PWM_DRIVER_CONFIG_BANK_SIZE == PWM_BANK_C) {
        max_hz = 1000u;
    }

    return frequency_hz >= min_hz && frequency_hz <= max_hz;
}

bool pwm_driver_config_is_monitor(void) {
    for (pwm_bank_t bank = PWM_BANK_A; bank < PWM_BANK_COUNT; ++bank) {
        if (pwm_driver_config_table_configured &&
            pwm_driver_config_bank_roles[bank] == PWM_ROLE_MONITOR) {
            return true;
        }
    }
    return false;
}

/** @brief Record the bank roles before Core 1 launches; false when already set or invalid. */
static bool pwm_driver_configure_table(const pwm_role_t roles[PWM_BANK_COUNT]) {
    if ((roles == NULL) || pwm_driver_config_table_configured) {
        return false;
    }

    for (pwm_bank_t bank = PWM_BANK_A; bank < PWM_BANK_COUNT; ++bank) {
        if ((roles[bank] != PWM_ROLE_GENERATOR) &&
            (roles[bank] != PWM_ROLE_MONITOR)) {
            return false;
        }
    }

    memcpy(pwm_driver_config_bank_roles, roles, sizeof(pwm_driver_config_bank_roles));
    pwm_driver_config_table_configured = true;
    if (!pwm_driver_config_validate()) {
        pwm_driver_config_table_configured = false;
        return false;
    }
    return true;
}


static inline void pwm_driver_memory_barrier(void) {
#if defined(__arm__) || defined(__thumb__)
    __asm volatile("dmb ish" ::: "memory");
#else
    __sync_synchronize();
#endif
}

/** @brief Timeout for one admitted cross-core apply request in milliseconds. */
#define PWM_DRIVER_APPLY_TIMEOUT_MS 1000

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
/** @brief Backend initialization operation signature used by the routing table. */
typedef bool (*pwm_driver_backend_init_fn_t)(void);
/** @brief Read one backend-local monitor sample. */
typedef bool (*pwm_driver_backend_get_fn_t)(uint channel, pwm_driver_state_t *state);
/** @brief Backend-owned readback finalization signature used by the routing table. */
typedef void (*pwm_driver_backend_finalize_readback_fn_t)(uint channel, pwm_driver_state_t *state,
                                                          uint64_t pulse_ref_us);

/** @brief One in-flight cross-core mailbox command record. */
typedef struct {
    uint8_t channel;    /**< Logical channel index. */
    uint32_t freq_hz;   /**< Requested frequency in Hz. */
    uint8_t duty;       /**< Requested duty. */
} pwm_driver_cmd_t;

/** @brief Reply record published by Core 1 after one mailbox apply attempt. */
typedef struct {
    bool ok; /**< Indicates whether the backend accepted the command. */
} pwm_driver_reply_t;

/** @brief Mailbox lifecycle states for the one-slot Core 0/Core 1 command exchange. */
typedef enum {
    PWM_DRIVER_MAILBOX_IDLE = 0, /**< No command is pending or waiting for collection. */
    PWM_DRIVER_MAILBOX_PENDING,  /**< Core 0 published a command that Core 1 has not claimed yet. */
    PWM_DRIVER_MAILBOX_ACTIVE,   /**< Core 1 claimed the command and is applying it. */
    PWM_DRIVER_MAILBOX_COMPLETE, /**< Core 1 published a reply for the last admitted command. */
} pwm_driver_mailbox_state_t;

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

/** @brief Backend routing table indexed by `[bank][role]`; each bank owns one fixed module. */
static const pwm_driver_backend_t pwm_driver_backends[PWM_BANK_COUNT][2] = {
    [PWM_BANK_A] =
        {
            [PWM_ROLE_GENERATOR] = {.init = hw_gen_init,
                                                       .set = hw_gen_set,
                                                       .finalize_readback = hw_gen_finalize_readback},
            [PWM_ROLE_MONITOR] = {.init = hw_mon_init, .get = hw_mon_get},
        },
    [PWM_BANK_B] =
        {
            [PWM_ROLE_GENERATOR] = {.init = pio_gen_init,
                                                       .set = pio_gen_set,
                                                       .finalize_readback = pio_gen_finalize_readback},
            [PWM_ROLE_MONITOR] = {.init = pio_mon_init, .get = pio_mon_get},
        },
    [PWM_BANK_C] =
        {
            [PWM_ROLE_GENERATOR] = {.init = sw_gen_init, .set = sw_gen_set},
            [PWM_ROLE_MONITOR] = {.init = sw_mon_init, .get = sw_mon_get},
        },
};

/** @brief Resolve the module that is active for one bank under the startup roles. */
static const pwm_driver_backend_t *pwm_driver_bank_backend(pwm_bank_t bank) {
    pwm_role_t role;

    if (!pwm_driver_config_channel_role((uint)bank * PWM_DRIVER_CONFIG_BANK_SIZE, &role)) {
        return NULL;
    }
    return &pwm_driver_backends[bank][role];
}

/** @brief Dispatch the Pico SDK's single GPIO callback to the software monitor bank. */
static void pwm_driver_monitor_gpio_irq(uint gpio, uint32_t events) {
    sw_mon_handle_gpio_irq(gpio, events);
}

/** @brief Resolve one logical channel into its bank module and bank-local index. */
static const pwm_driver_backend_t *pwm_driver_get_backend(uint channel, uint *local_channel) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT) {
        return NULL;
    }

    if (local_channel != NULL) {
        *local_channel = channel % PWM_DRIVER_CONFIG_BANK_SIZE;
    }

    return pwm_driver_bank_backend((pwm_bank_t)(channel / PWM_DRIVER_CONFIG_BANK_SIZE));
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

    backend = pwm_driver_get_backend(channel, &local_channel);
    if (backend == NULL || backend->set == NULL) {
        return false;
    }

    return backend->set(local_channel, freq_hz, duty);
}

/** @brief Initialize every backend selected by the already-populated startup table. */
static bool pwm_driver_initialize_backends(void) {
    for (pwm_bank_t bank = PWM_BANK_A; bank < PWM_BANK_COUNT; ++bank) {
        const pwm_driver_backend_t *backend = pwm_driver_bank_backend(bank);

        if ((backend == NULL) || (backend->init == NULL) || !backend->init()) {
            return false;
        }
    }

    return true;
}

/** @brief Publish fresh monitor samples from Core 1 into the shared state cache. */
static void pwm_driver_refresh_monitor_state(void) {
    for (uint channel = 0u; channel < PWM_DRIVER_CHANNEL_COUNT; ++channel) {
        const pwm_driver_backend_t *backend;
        pwm_driver_state_t state;
        uint local_channel;

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

        bool ok = pwm_driver_backend_set(cmd.channel, cmd.freq_hz, cmd.duty);

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

bool pwm_driver_init(const pwm_role_t roles[PWM_BANK_COUNT]) {
    if ((roles == NULL) || (pwm_lifecycle != PWM_DRIVER_LIFECYCLE_RESET) || !pwm_driver_configure_table(roles)) {
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

    if (cmd->channel >= PWM_DRIVER_CHANNEL_COUNT) {
        return PWM_DRIVER_RESULT_INVALID;
    }

    critical_section_enter_blocking(&pwm_reply_lock);
    if (pwm_mailbox.state != PWM_DRIVER_MAILBOX_IDLE && pwm_mailbox.state != PWM_DRIVER_MAILBOX_COMPLETE) {
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

        critical_section_enter_blocking(&pwm_reply_lock);
        if (pwm_mailbox.state == PWM_DRIVER_MAILBOX_COMPLETE) {
            reply = pwm_mailbox.reply;
            pwm_mailbox.state = PWM_DRIVER_MAILBOX_IDLE;
            critical_section_exit(&pwm_reply_lock);
            return reply.ok ? PWM_DRIVER_RESULT_OK : PWM_DRIVER_RESULT_APPLY_FAILED;
        }

        /* Still unclaimed at the deadline: drop it. Once Core 1 is ACTIVE, keep waiting for the real result. */
        timed_out = time_reached(deadline);
        if (timed_out && pwm_mailbox.state == PWM_DRIVER_MAILBOX_PENDING) {
            pwm_mailbox.state = PWM_DRIVER_MAILBOX_IDLE;
            cancelled = true;
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
    pwm_role_t role;
    pwm_driver_result_t result;

    if (!pwm_driver_config_channel_role(channel, &role)) {
        return PWM_DRIVER_RESULT_INVALID;
    }
    if (role == PWM_ROLE_MONITOR) {
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

    if (state == NULL) {
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
    pwm_driver_result_t status = PWM_DRIVER_RESULT_OK;

    /* Restoring defaults means stopping every generator channel; monitors have nothing to restore. */
    for (uint channel = 0u; channel < PWM_DRIVER_CHANNEL_COUNT && status == PWM_DRIVER_RESULT_OK; ++channel) {
        pwm_role_t role;

        if (pwm_driver_config_channel_role(channel, &role) && role == PWM_ROLE_GENERATOR) {
            status = pwm_driver_set(channel, 0u, 0u);
        }
    }

    return status;
}
