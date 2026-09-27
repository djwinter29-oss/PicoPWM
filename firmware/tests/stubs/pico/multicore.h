#ifndef TEST_PICO_MULTICORE_H
#define TEST_PICO_MULTICORE_H

#include <assert.h>
#include <stdbool.h>

static int test_multicore_lockout_victim_ready;

static inline void multicore_lockout_victim_init(void) {
    test_multicore_lockout_victim_ready = 1;
}

static inline bool multicore_lockout_victim_is_initialized(unsigned core_num) {
    (void)core_num;
    return test_multicore_lockout_victim_ready != 0;
}

static inline void multicore_lockout_start_blocking(void) {
    assert(test_multicore_lockout_victim_ready);
}

static inline void multicore_lockout_end_blocking(void) {}

#endif
