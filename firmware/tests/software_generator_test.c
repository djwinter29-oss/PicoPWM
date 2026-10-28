#include "driver/pwm/software/software_generator.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#include <assert.h>
#include <stdio.h>

int sw_test_gpio_level[32];
alarm_callback_t sw_test_tick;
bool sw_test_pool_fail;
bool sw_test_arm_fail;
unsigned sw_test_pool_core;

const software_pwm_config_t software_pwm_config = {
    .channels = {{16u}, {17u}, {18u}, {19u}, {20u}, {21u}, {22u}, {28u}}};

static alarm_pool_t pool;
static uint32_t published_freq;
static uint8_t published_duty;
static uint published_channel = 99u;

void gpio_init(uint gpio) {
    (void)gpio;
}
void gpio_set_dir(uint gpio, int direction) {
    (void)gpio;
    (void)direction;
}
void gpio_put(uint gpio, int value) {
    assert(gpio < 32u);
    sw_test_gpio_level[gpio] = value;
}

alarm_pool_t *alarm_pool_create_with_unused_hardware_alarm(uint alarms) {
    (void)alarms;
    return sw_test_pool_fail ? NULL : &pool;
}
uint alarm_pool_core_num(alarm_pool_t *created) {
    (void)created;
    return sw_test_pool_core;
}
bool alarm_pool_add_repeating_timer_us(alarm_pool_t *created, int64_t delay_us, alarm_callback_t callback,
                                       void *user_data, repeating_timer_t *timer) {
    (void)created;
    (void)delay_us;
    (void)user_data;
    (void)timer;
    sw_test_tick = callback;
    return !sw_test_arm_fail;
}

void pwm_driver_store_applied_state(uint channel, const pwm_state_t *state) {
    published_channel = channel;
    published_freq = state->freq_hz;
    published_duty = state->duty;
}

int main(void) {
    sw_test_pool_fail = true;
    assert(!sw_gen_init());
    sw_test_pool_fail = false;
    sw_test_pool_core = 1u;
    assert(!sw_gen_init());
    sw_test_pool_core = 0u;
    assert(sw_gen_init());
    assert(sw_test_gpio_level[16] == 0);

    assert(sw_gen_set(0u, 1000u, 250u));
    assert(published_duty == 100u);
    assert(!sw_gen_set(8u, 1000u, 50u));
    assert(!sw_gen_set(0u, 1001u, 50u));
    assert(sw_gen_set(0u, 0u, 0u));
    assert(published_channel == 16u && published_freq == 0u && published_duty == 0u);

    sw_test_arm_fail = true;
    assert(!sw_gen_set(0u, 1000u, 50u));
    sw_test_arm_fail = false;
    assert(sw_gen_set(0u, 1000u, 50u));
    assert(published_freq == 1000u && published_duty == 50u);
    assert(sw_test_tick != NULL);

    assert(sw_test_tick(NULL));
    assert(sw_test_gpio_level[16] == 1);
    for (int i = 0; i < 49; ++i) {
        assert(sw_test_tick(NULL));
    }
    assert(sw_test_gpio_level[16] == 0);

    assert(sw_gen_set(0u, 0u, 100u));
    assert(published_duty == 100u && sw_test_gpio_level[16] == 1);
    assert(!sw_test_tick(NULL));
    puts("software_generator_test ok");
    return 0;
}
