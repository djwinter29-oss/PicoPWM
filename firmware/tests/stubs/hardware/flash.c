// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

#include "hardware/flash.h"

#include <string.h>

uint8_t test_flash[TEST_FLASH_SIZE];

void test_flash_reset(void) {
    memset(test_flash, 0xff, sizeof(test_flash));
}

void flash_range_erase(uint32_t offset, size_t count) {
    test_save_trace(TEST_SAVE_ERASE);
    memset(test_flash + offset, 0xff, count);
}

void flash_range_program(uint32_t offset, const uint8_t *data, size_t count) {
    test_save_trace(TEST_SAVE_PROGRAM);
    memcpy(test_flash + offset, data, count);
}
