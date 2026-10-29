#include "driver/pwm/software/software_monitor.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#include <assert.h>
#include <stdio.h>

const software_pwm_config_t software_pwm_config = {
    .channels = {{16u}, {17u}, {18u}, {19u}, {20u}, {21u}, {22u}, {28u}}};
static alarm_pool_t pool;
static alarm_callback_t tick;
static uint64_t now;
static uint32_t levels;
static bool pool_fail, arm_fail;
static uint pool_core;
static unsigned reads;

uint64_t time_us_64(void) {
    return now;
}
uint32_t gpio_get_all(void) {
    reads++;
    return levels;
}
void gpio_init(uint pin) {
    assert(pin < 30u);
}
void gpio_set_dir(uint pin, int dir) {
    assert(pin < 30u && dir == GPIO_IN);
}
alarm_pool_t *alarm_pool_create_with_unused_hardware_alarm(uint alarms) {
    assert(alarms == 1u);
    return pool_fail ? NULL : &pool;
}
uint alarm_pool_core_num(alarm_pool_t *p) {
    assert(p == &pool);
    return pool_core;
}
bool alarm_pool_add_repeating_timer_us(alarm_pool_t *p, int64_t delay, alarm_callback_t callback, void *data,
                                       repeating_timer_t *timer) {
    assert(p == &pool && delay == 10 && data == NULL && timer != NULL);
    tick = callback;
    return !arm_fail;
}

static void scan(uint64_t timestamp, uint32_t bitmap) {
    now = timestamp;
    levels = bitmap;
    unsigned before = reads;
    assert(tick(NULL));
    assert(reads == before + 1u);
}

static void expect(uint channel, uint freq, uint duty) {
    pwm_state_t state;
    assert(sw_mon_get(channel, &state));
    assert(state.freq_hz == freq && state.duty == duty);
}

int main(void) {
    pwm_state_t state;
    assert(!sw_mon_get(0, &state));
    pool_fail = true;
    assert(!sw_mon_init());
    pool_fail = false;
    pool_core = 1;
    assert(!sw_mon_init());
    pool_core = 0;
    arm_fail = true;
    assert(!sw_mon_init());
    arm_fail = false;
    assert(sw_mon_init());
    assert(sw_mon_init());
    assert(!sw_mon_get(8, &state) && !sw_mon_get(0, NULL));
    assert(!sw_mon_get(0, &state));

    uint32_t mask = 0;
    for (uint c = 0; c < 8; ++c) {
        mask |= 1u << software_pwm_config.channels[c].gpio;
    }
    /* Eight simultaneous 1 kHz signals with alternating 8 us timestamp error. */
    for (uint cycle = 0; cycle <= 40; ++cycle) {
        uint jitter = cycle % 2u ? 8u : 0u;
        scan(100u + cycle * 1000u + jitter, mask);
        scan(600u + cycle * 1000u + jitter, 0);
    }
    for (uint c = 0; c < 8; ++c) {
        expect(c, 1000, 50);
    }
    /* Static timeout, followed by a fresh slow cycle without old-window contamination. */
    scan(1100000u, 0);
    expect(0, 0, 0);
    scan(1200000u, mask);
    assert(!sw_mon_get(0, &state));
    scan(1225000u, 0);
    scan(1300000u, mask);
    expect(0, 10, 25);
    scan(2400000u, mask);
    expect(0, 0, 100);

    /* Timestamp handling remains correct past the 32-bit microsecond wrap. */
    uint64_t start = (uint64_t)UINT32_MAX + 2000000u;
    scan(start, 0);
    for (uint cycle = 0; cycle <= 20; ++cycle) {
        scan(start + 100u + cycle * 1000u, mask);
        scan(start + 600u + cycle * 1000u, 0);
    }
    expect(0, 1000, 50);

    /* Each channel independently measures a different period and duty. */
    start += 100000u;
    for (uint t = 0; t <= 250000u; t += 10u) {
        uint32_t bitmap = 0u;
        for (uint c = 0; c < 8; ++c) {
            uint period = (c + 1u) * 1000u;
            if (t % period < period / 4u) {
                bitmap |= 1u << software_pwm_config.channels[c].gpio;
            }
        }
        scan(start + t, bitmap);
    }
    for (uint c = 0; c < 8; ++c) {
        pwm_state_t measured;
        assert(sw_mon_get(c, &measured));
        uint expected = (1000000u + (c + 1u) * 500u) / ((c + 1u) * 1000u);
        assert(measured.freq_hz == expected && measured.duty == 25u);
    }

    /* A 300 kHz input aliases at 10 us sampling, but adds no callback work or GPIO IRQs. */
    start += 300000u;
    unsigned before = reads;
    for (uint t = 0; t < 10000u; t += 10u) {
        scan(start + t, ((uint64_t)t * 300000u % 1000000u) < 500000u ? mask : 0u);
    }
    assert(reads - before == 1000u);
    puts("software_monitor_test ok");
    return 0;
}
