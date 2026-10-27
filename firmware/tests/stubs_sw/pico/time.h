#ifndef TEST_SW_TIME_H
#define TEST_SW_TIME_H

#include <stdbool.h>
#include <stdint.h>

typedef struct alarm_pool {
    int unused;
} alarm_pool_t;
typedef struct repeating_timer {
    int unused;
} repeating_timer_t;
typedef bool (*alarm_callback_t)(repeating_timer_t *timer);

extern alarm_callback_t sw_test_tick;
extern bool sw_test_pool_fail;
extern bool sw_test_arm_fail;
extern unsigned sw_test_pool_core;

alarm_pool_t *alarm_pool_create_with_unused_hardware_alarm(uint alarms);
uint alarm_pool_core_num(alarm_pool_t *pool);
bool alarm_pool_add_repeating_timer_us(alarm_pool_t *pool, int64_t delay_us, alarm_callback_t callback, void *user_data,
                                       repeating_timer_t *timer);

#endif
