/**
 * @file sdk_stub.h
 * @brief Host stand-in for the Pico SDK calls used by `pwm_driver.c`, with Core 1 simulated by a thread.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#ifndef TEST_PWM_DRIVER_SDK_STUB_H
#define TEST_PWM_DRIVER_SDK_STUB_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

typedef unsigned int uint;
typedef uint64_t absolute_time_t;
typedef pthread_mutex_t critical_section_t;
typedef pthread_mutex_t mutex_t;

enum { PWM_CHAN_A = 0u, PWM_CHAN_B = 1u };

/** @brief Core number of the calling thread: 0 for the test thread, 1 for the simulated Core 1. */
static __thread unsigned test_core_num;

static inline unsigned get_core_num(void) {
    return test_core_num;
}
static inline void __sev(void) {}
static inline uint32_t save_and_disable_interrupts(void) {
    return 0u;
}
static inline void restore_interrupts(uint32_t state) {
    (void)state;
}
static inline unsigned pwm_gpio_to_channel(unsigned gpio) {
    return gpio & 1u;
}
static inline void gpio_set_irq_callback(void (*cb)(uint, uint32_t)) {
    (void)cb;
}
static inline void multicore_lockout_victim_init(void) {}

static inline absolute_time_t get_absolute_time(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
static inline absolute_time_t make_timeout_time_us(uint64_t us) {
    return get_absolute_time() + us;
}
static inline absolute_time_t make_timeout_time_ms(uint32_t ms) {
    return make_timeout_time_us((uint64_t)ms * 1000u);
}
static inline bool time_reached(absolute_time_t t) {
    return get_absolute_time() >= t;
}
static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
    return (int64_t)(to - from);
}
static inline bool best_effort_wfe_or_timeout(absolute_time_t t) {
    (void)t;
    usleep(200);
    return false;
}

static inline void critical_section_init(critical_section_t *cs) {
    pthread_mutex_init(cs, NULL);
}
static inline void critical_section_enter_blocking(critical_section_t *cs) {
    pthread_mutex_lock(cs);
}
static inline void critical_section_exit(critical_section_t *cs) {
    pthread_mutex_unlock(cs);
}
static inline void mutex_init(mutex_t *m) {
    pthread_mutex_init(m, NULL);
}
static inline void mutex_enter_blocking(mutex_t *m) {
    pthread_mutex_lock(m);
}
static inline void mutex_exit(mutex_t *m) {
    pthread_mutex_unlock(m);
}

static void (*test_core1_entry)(void);

static void *test_core1_trampoline(void *arg) {
    (void)arg;
    test_core_num = 1u;
    test_core1_entry();
    return NULL;
}

static inline void multicore_launch_core1(void (*entry)(void)) {
    pthread_t thread;

    test_core1_entry = entry;
    pthread_create(&thread, NULL, test_core1_trampoline, NULL);
    pthread_detach(thread);
}

#endif
