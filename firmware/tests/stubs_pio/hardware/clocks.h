// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

#ifndef TEST_PIO_CLOCKS_H
#define TEST_PIO_CLOCKS_H

#include <stdint.h>

enum { clk_sys = 0 };

static inline uint32_t clock_get_hz(uint clk) {
    (void)clk;
    return 125000000u;
}

#endif
