/**
 * @file sync.h
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#ifndef TEST_HARDWARE_SYNC_H
#define TEST_HARDWARE_SYNC_H

#include <stdint.h>

static inline uint32_t save_and_disable_interrupts(void) {
    return 0u;
}

static inline void restore_interrupts(uint32_t state) {
    (void)state;
}

#endif
