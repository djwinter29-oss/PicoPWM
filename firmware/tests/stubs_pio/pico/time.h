/**
 * @file time.h
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#ifndef TEST_PIO_TIME_H
#define TEST_PIO_TIME_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef uint64_t absolute_time_t;

static inline absolute_time_t get_absolute_time(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
static inline absolute_time_t make_timeout_time_us(uint64_t us) {
    return get_absolute_time() + us;
}
static inline bool time_reached(absolute_time_t deadline) {
    return get_absolute_time() >= deadline;
}
static inline void tight_loop_contents(void) {}

#endif
