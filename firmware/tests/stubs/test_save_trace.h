// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

#ifndef TEST_SAVE_TRACE_H
#define TEST_SAVE_TRACE_H

/* Order of one programming save. boot_config_test records these; other tests ignore them. */
enum {
    TEST_SAVE_KICK = 1,
    TEST_SAVE_IRQ_OFF,
    TEST_SAVE_LOCK,
    TEST_SAVE_ERASE,
    TEST_SAVE_PROGRAM,
    TEST_SAVE_UNLOCK,
    TEST_SAVE_IRQ_ON,
};

#ifdef BOOT_CONFIG_SAVE_TRACE
void test_save_trace(int event);
#else
#define test_save_trace(event) ((void)(event))
#endif

#endif
