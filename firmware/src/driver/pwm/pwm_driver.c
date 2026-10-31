// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

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

#include "hardware/hardware_math.h"
#include "pwm_math.h"

#include <string.h>

/** @brief Bank roles chosen at startup; set once before Core 1 launches. */
static pwm_role_t pwm_driver_config_bank_roles[PWM_BANK_COUNT];

uint8_t pwm_driver_channel_gpio(uint channel) {
    uint index = channel % PWM_DRIVER_CONFIG_BANK_SIZE;
    switch (channel / PWM_DRIVER_CONFIG_BANK_SIZE) {
    case PWM_BANK_A:
        return (uint8_t)hardware_pwm_config.channels[index].gpio;
    case PWM_BANK_B:
        return (uint8_t)pio_pwm_config.channels[index].gpio;
    case PWM_BANK_C:
        return (uint8_t)software_pwm_config.channels[index].gpio;
    default:
        return UINT8_MAX;
    }
}

bool pwm_driver_config_channel_role(uint channel, pwm_role_t *role) {
    if ((role == NULL) || (channel >= PWM_DRIVER_CONFIG_CHANNEL_COUNT)) {
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
        max_hz = 10000000u;
    } else if (channel / PWM_DRIVER_CONFIG_BANK_SIZE == PWM_BANK_B && role == PWM_ROLE_GENERATOR) {
        max_hz = 10000000u;
    } else if (channel / PWM_DRIVER_CONFIG_BANK_SIZE == PWM_BANK_C) {
        max_hz = role == PWM_ROLE_GENERATOR ? 10000u : 1000u;
    }

    return frequency_hz >= min_hz && frequency_hz <= max_hz;
}

bool pwm_driver_config_is_monitor(void) {
    for (pwm_bank_t bank = PWM_BANK_A; bank < PWM_BANK_COUNT; ++bank) {
        if (pwm_driver_config_bank_roles[bank] == PWM_ROLE_MONITOR) {
            return true;
        }
    }
    return false;
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
typedef bool (*pwm_driver_backend_get_fn_t)(uint channel, pwm_state_t *state);

/** @brief One in-flight cross-core mailbox command record. */
typedef struct {
    uint8_t channel;  /**< Logical channel index. */
    uint32_t freq_hz; /**< Requested frequency in Hz. */
    uint8_t duty;     /**< Requested duty. */
} pwm_driver_cmd_t;

/**
 * @brief One-slot Core 0/Core 1 mailbox; every field is accessed under @ref pwm_lock.
 *
 * Core 0 sets `pending`. Core 1 clears it when it claims the command, so `!pending && !done`
 * means the command is being applied. Core 0 may only cancel while `pending` is still set.
 */
typedef struct {
    pwm_driver_cmd_t cmd; /**< Command published by Core 0. */
    bool pending;         /**< Published and not yet claimed by Core 1. */
    bool done;            /**< Core 1 finished applying the command. */
    bool ok;              /**< Backend result, valid when `done` is set. */
} pwm_driver_mailbox_t;

/** @brief Routing descriptor for one logical backend bank. */
typedef struct {
    pwm_driver_backend_init_fn_t init; /**< Backend initialization callback. */
    pwm_driver_backend_set_fn_t set;   /**< Backend-local set callback. */
    pwm_driver_backend_get_fn_t get;   /**< Optional backend-local read callback. */
} pwm_driver_backend_t;

/** @brief Core 1 loop counter used to detect a stalled backend owner. */
static volatile uint32_t pwm_core1_heartbeat;
/** @brief Core 1-published service work fraction in basis points; one aligned atomic read on Core 0. */
static volatile uint32_t pwm_core1_service_load;

uint16_t pwm_driver_service_load(void) {
    return (uint16_t)pwm_core1_service_load;
}
/** @brief Last Core 1 heartbeat observed by a Core 0 mailbox wait. */
static uint32_t pwm_core1_heartbeat_seen;
/** @brief True when any bank runs as a monitor, so Core 1 must poll samples. */
static bool pwm_has_monitor;
/** @brief Time of the last observed Core 1 heartbeat change. */
static absolute_time_t pwm_core1_heartbeat_stamp;
/** @brief Optional Core 0 callback invoked while a mailbox apply is still running. */
static void (*pwm_wait_hook)(void);
/** @brief True while @ref pwm_wait_hook is on the stack, so a nested submit fails instead of deadlocking. */
static bool pwm_wait_hook_active;

void pwm_driver_set_wait_hook(void (*hook)(void)) {
    pwm_wait_hook = hook;
}
/** @brief Critical section protecting mailbox request and reply records. */
static critical_section_t pwm_lock;
/** @brief Core 0 serialization lock shared by the public control entry points. */
static mutex_t control_api_lock;
/** @brief Published realized-state cache for all logical channels, guarded by @ref pwm_lock. */
static pwm_state_t pwm_state_cache[PWM_DRIVER_CHANNEL_COUNT];
/** @brief Last monitor sample published by Core 1; used to skip unchanged publications. */
static pwm_state_t pwm_monitor_last[PWM_DRIVER_CHANNEL_COUNT];
/** @brief One-slot Core 0/Core 1 mailbox shared between submitter and backend owner. */
static pwm_driver_mailbox_t pwm_mailbox;

/** @brief Backend routing table indexed by `[bank][role]`; each bank owns one fixed module. */
static const pwm_driver_backend_t pwm_driver_backends[PWM_BANK_COUNT][2] = {
    [PWM_BANK_A] =
        {
            [PWM_ROLE_GENERATOR] = {.init = hw_gen_init, .set = hw_gen_set},
            [PWM_ROLE_MONITOR] = {.init = hw_mon_init, .get = hw_mon_get},
        },
    [PWM_BANK_B] =
        {
            [PWM_ROLE_GENERATOR] = {.init = pio_gen_init, .set = pio_gen_set},
            [PWM_ROLE_MONITOR] = {.init = pio_mon_init, .get = pio_mon_get},
        },
    [PWM_BANK_C] =
        {
            [PWM_ROLE_GENERATOR] = {.init = sw_gen_init, .set = sw_gen_set},
            [PWM_ROLE_MONITOR] = {.init = sw_mon_init, .get = sw_mon_get},
        },
};

/** @brief Resolve the module that is active for one bank under the startup roles. */
static const pwm_driver_backend_t *pwm_driver_bank_backend(uint bank) {
    return &pwm_driver_backends[bank][pwm_driver_config_bank_roles[bank]];
}

/**
 * @brief Submit one cross-core mailbox request while the public write lock is already held.
 * @param cmd Caller-owned mailbox request descriptor.
 * @return Result code for the admitted command attempt.
 */
static pwm_driver_result_t pwm_driver_submit_locked(const pwm_driver_cmd_t *cmd);

/** @copydoc pwm_driver_store_applied_state */
void pwm_driver_store_applied_state(uint channel, const pwm_state_t *state) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT || state == NULL) {
        return;
    }

    critical_section_enter_blocking(&pwm_lock);
    pwm_state_cache[channel] = *state;
    critical_section_exit(&pwm_lock);
}

/**
 * @brief Dispatch one logical channel write to the owning backend implementation.
 * @param channel Logical channel index.
 * @param freq_hz Requested frequency in Hz.
 * @param duty Requested duty in percent in the range `[0, 100]`.
 * @return `true` when the backend accepted the request.
 */
static bool pwm_driver_backend_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    const pwm_driver_backend_t *backend = pwm_driver_bank_backend(channel / PWM_DRIVER_CONFIG_BANK_SIZE);

    return backend->set != NULL && backend->set(channel % PWM_DRIVER_CONFIG_BANK_SIZE, freq_hz, duty);
}

/** @brief Initialize every backend selected by the already-populated startup table. */
static bool pwm_driver_initialize_backends(void) {
    for (uint bank = 0u; bank < PWM_BANK_COUNT; ++bank) {
        const pwm_driver_backend_t *backend = pwm_driver_bank_backend(bank);

        if (backend->init == NULL || !backend->init()) {
            return false;
        }
    }

    return true;
}

/** @brief Publish changed monitor samples from Core 1 into the shared state cache. */
static void pwm_driver_refresh_monitor_state(void) {
    for (uint bank = 0u; bank < PWM_BANK_COUNT; ++bank) {
        const pwm_driver_backend_t *backend = pwm_driver_bank_backend(bank);

        if (backend->get == NULL) {
            continue;
        }
        for (uint local = 0u; local < PWM_DRIVER_CONFIG_BANK_SIZE; ++local) {
            uint channel = bank * PWM_DRIVER_CONFIG_BANK_SIZE + local;
            pwm_state_t state;

            /* No valid sample (signal lost or not yet captured) reads as 0 Hz, 0% rather than a stale value. */
            if (!backend->get(local, &state)) {
                state = (pwm_state_t){0};
            }
            if (state.freq_hz != pwm_monitor_last[channel].freq_hz || state.duty != pwm_monitor_last[channel].duty) {
                pwm_monitor_last[channel] = state;
                pwm_driver_store_applied_state(channel, &state);
            }
        }
    }
}

/** @brief Zero the published cache; the logical power-on default is 0 Hz, 0%. */
static void pwm_driver_cache_defaults(void) {
    memset(pwm_state_cache, 0, sizeof(pwm_state_cache));
}

/** @brief Claim and process all currently queued mailbox commands on Core 1. */
static void pwm_driver_process_mailbox(void) {
    pwm_driver_cmd_t cmd;
    bool has_cmd;

    critical_section_enter_blocking(&pwm_lock);
    has_cmd = pwm_mailbox.pending;
    if (has_cmd) {
        cmd = pwm_mailbox.cmd;
        pwm_mailbox.pending = false;
    }
    critical_section_exit(&pwm_lock);

    if (has_cmd) {
        bool ok = pwm_driver_backend_set(cmd.channel, cmd.freq_hz, cmd.duty);

        critical_section_enter_blocking(&pwm_lock);
        pwm_mailbox.ok = ok;
        pwm_mailbox.done = true;
        critical_section_exit(&pwm_lock);
        __sev();
    }
}

/** @brief Return false when Core 1 has not advanced its loop counter within the apply timeout. */
static bool pwm_driver_core1_heartbeat_ok(void) {
    uint32_t heartbeat;

    pwm_driver_memory_barrier();
    heartbeat = pwm_core1_heartbeat;
    if (heartbeat != pwm_core1_heartbeat_seen) {
        pwm_core1_heartbeat_seen = heartbeat;
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

    if (!pwm_driver_initialize_backends()) {
        pwm_lifecycle = PWM_DRIVER_LIFECYCLE_FAILED;
        return;
    }

    pwm_lifecycle = PWM_DRIVER_LIFECYCLE_READY;

    absolute_time_t service_window = get_absolute_time();
    uint64_t service_busy_us = 0u;
    while (true) {
        absolute_time_t service_start = get_absolute_time();
        pwm_core1_heartbeat++;
        pwm_driver_memory_barrier();
        pwm_driver_process_mailbox();
        if (pwm_has_monitor) {
            pwm_driver_refresh_monitor_state();
        }
        absolute_time_t now = get_absolute_time();
        service_busy_us += (uint64_t)absolute_time_diff_us(service_start, now);
        uint64_t window_us = (uint64_t)absolute_time_diff_us(service_window, now);
        if (window_us >= 1000000u) {
            uint64_t load = service_busy_us * 10000u / window_us;
            pwm_core1_service_load = (uint32_t)(load > 10000u ? 10000u : load);
            service_busy_us = 0u;
            service_window = now;
        }
        /* SDK alarm setup can wake early. Keep the deadline unless a command needs service. */
        absolute_time_t idle_deadline = make_timeout_time_us(pwm_has_monitor ? 1000 : 100000);
        while (!time_reached(idle_deadline)) {
            best_effort_wfe_or_timeout(idle_deadline);
            critical_section_enter_blocking(&pwm_lock);
            bool pending = pwm_mailbox.pending;
            critical_section_exit(&pwm_lock);
            if (pending) {
                break;
            }
        }
    }
}

bool pwm_driver_init(const pwm_role_t roles[PWM_BANK_COUNT]) {
    if ((roles == NULL) || (pwm_lifecycle != PWM_DRIVER_LIFECYCLE_RESET)) {
        return false;
    }

    /* Roles are validated by boot_config before this call. */
    memcpy(pwm_driver_config_bank_roles, roles, sizeof(pwm_driver_config_bank_roles));
    for (uint bank = 0u; bank < PWM_BANK_COUNT; ++bank) {
        pwm_has_monitor |= roles[bank] == PWM_ROLE_MONITOR;
    }
    pwm_lifecycle = PWM_DRIVER_LIFECYCLE_STARTING;
    critical_section_init(&pwm_lock);
    mutex_init(&control_api_lock);
    pwm_driver_cache_defaults();
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
    bool ok;

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

    /* ponytail: control_api_lock admits one submitter and the wait below never abandons a claimed command,
     * so the slot is always free here. A second concurrent submitter would need a BUSY check.
     */
    critical_section_enter_blocking(&pwm_lock);
    pwm_mailbox.cmd = *cmd;
    pwm_mailbox.done = false;
    pwm_mailbox.pending = true;
    critical_section_exit(&pwm_lock);

    __sev();
    deadline = make_timeout_time_ms(PWM_DRIVER_APPLY_TIMEOUT_MS);
    pwm_driver_memory_barrier();
    pwm_core1_heartbeat_seen = pwm_core1_heartbeat;
    pwm_core1_heartbeat_stamp = get_absolute_time();

    do {
        bool timed_out = false;
        bool cancelled = false;

        critical_section_enter_blocking(&pwm_lock);
        if (pwm_mailbox.done) {
            ok = pwm_mailbox.ok;
            critical_section_exit(&pwm_lock);
            return ok ? PWM_DRIVER_RESULT_OK : PWM_DRIVER_RESULT_APPLY_FAILED;
        }

        /* Still unclaimed at the deadline: drop it. Once Core 1 has claimed it, keep waiting for the real result. */
        timed_out = time_reached(deadline);
        if (timed_out && pwm_mailbox.pending) {
            pwm_mailbox.pending = false;
            cancelled = true;
        }
        critical_section_exit(&pwm_lock);

        /*
         * A live Core 1 loop heartbeats at least every 100 ms. No progress
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
            pwm_wait_hook_active = true;
            pwm_wait_hook();
            pwm_wait_hook_active = false;
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
    if (channel / PWM_DRIVER_CONFIG_BANK_SIZE == PWM_BANK_A && role == PWM_ROLE_GENERATOR) {
        bool static_high = false;
        uint8_t static_duty = 0u;
        uint32_t top;
        uint16_t div_x16;
        bool is_static = pwm_generator_resolve_static(freq_hz, duty, &static_high, &static_duty);

        /* Static levels do not need a divider. A running PWM below the clock's counter floor is
         * INVALID here, matching the search hw_gen_set() would otherwise fail as APPLY_FAILED. */
        (void)static_high;
        (void)static_duty;
        if (!is_static && !pwm_hw_find_timing(system_clock_hz(), freq_hz, &top, &div_x16)) {
            return PWM_DRIVER_RESULT_INVALID;
        }
    }

    if (pwm_lifecycle != PWM_DRIVER_LIFECYCLE_READY) {
        return PWM_DRIVER_RESULT_UNAVAILABLE;
    }
    if (pwm_wait_hook_active) {
        return PWM_DRIVER_RESULT_BUSY;
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
bool pwm_driver_get(uint channel, pwm_state_t *state) {
    if (state == NULL || channel >= PWM_DRIVER_CHANNEL_COUNT || pwm_lifecycle == PWM_DRIVER_LIFECYCLE_RESET) {
        return false;
    }

    critical_section_enter_blocking(&pwm_lock);
    *state = pwm_state_cache[channel];
    critical_section_exit(&pwm_lock);
    return true;
}

/** @copydoc pwm_driver_restore_defaults */
pwm_driver_result_t pwm_driver_restore_defaults(void) {
    pwm_driver_result_t status = PWM_DRIVER_RESULT_OK;

    /* Restoring defaults means stopping every generator channel; monitors have nothing to restore. */
    for (uint channel = 0u; channel < PWM_DRIVER_CHANNEL_COUNT; ++channel) {
        pwm_role_t role;

        if (pwm_driver_config_channel_role(channel, &role) && role == PWM_ROLE_GENERATOR) {
            pwm_driver_result_t result = pwm_driver_set(channel, 0u, 0u);
            if ((status == PWM_DRIVER_RESULT_OK) && (result != PWM_DRIVER_RESULT_OK)) {
                status = result;
            }
        }
    }

    return status;
}
