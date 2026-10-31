// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

#ifndef TEST_HARDWARE_SYNC_H
#define TEST_HARDWARE_SYNC_H

#include "test_save_trace.h"

#include <stdint.h>

static inline uint32_t save_and_disable_interrupts(void) {
    test_save_trace(TEST_SAVE_IRQ_OFF);
    return 0u;
}

static inline void restore_interrupts(uint32_t state) {
    (void)state;
    test_save_trace(TEST_SAVE_IRQ_ON);
}

#endif
