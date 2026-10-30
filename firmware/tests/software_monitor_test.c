/**
 * @file software_monitor_test.c
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

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
    assert(p == &pool && delay == 2 && data == NULL && timer != NULL);
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

    /* Experimental 10 kHz target at all duty extremes, with 4 us actual scan spacing. */
    start += 2000000u;
    for (uint t = 0; t <= 100000u; t += 4u) {
        uint32_t bitmap = 0u;
        for (uint c = 0; c < 8; ++c) {
            uint high = c == 7u ? 90u : (c + 1u) * 10u;
            if ((t + c * 3u) % 100u < high) {
                bitmap |= 1u << software_pwm_config.channels[c].gpio;
            }
        }
        scan(start + t, bitmap);
    }
    for (uint c = 0; c < 8; ++c) {
        assert(sw_mon_get(c, &state));
        uint target_duty = c == 7u ? 90u : (c + 1u) * 10u;
        assert(state.freq_hz >= 9500u && state.freq_hz <= 10500u);
        assert(state.duty + 5u >= target_duty && state.duty <= target_duty + 5u);
    }
    sw_mon_scan_timing_t timing;
    sw_mon_get_scan_timing(&timing);
    assert(timing.min_gap_us == 4u);
    assert(timing.max_gap_us > 5u && timing.gaps_over_5us != 0u);
    unsigned long_gaps = timing.gaps_over_5us;
    scan(now + 12u, levels ^ (1u << 25u));
    sw_mon_get_scan_timing(&timing);
    assert(timing.gaps_over_5us == long_gaps + 1u);
    expect(0, 10000u, 12u);

    /* All nine duty targets and asynchronous scan phases with variable 3..5 us spacing. */
    for (uint duty = 10u; duty <= 90u; duty += 10u) {
        start = now + 2000000u;
        scan(start, 0u);
        for (uint t = 0u, n = 0u; t <= 50000u; t += 3u + n++ % 3u) {
            uint32_t bitmap = 0u;
            for (uint c = 0; c < 8u; ++c) {
                if ((t + c * 7u) % 100u < duty) {
                    bitmap |= 1u << software_pwm_config.channels[c].gpio;
                }
            }
            scan(start + 100u + t, bitmap);
        }
        for (uint c = 0; c < 8u; ++c) {
            assert(sw_mon_get(c, &state));
            assert(state.freq_hz >= 9500u && state.freq_hz <= 10500u);
            assert(state.duty + 5u >= duty && state.duty <= duty + 5u);
        }
    }

    /* A change to 2 Hz settles after a full cycle, including long high and low segments. */
    for (uint duty = 10u; duty <= 90u; duty += 40u) {
        start = now + 100u;
        scan(start, 0u);
        for (uint t = 0u; t <= 2000000u; t += 5u) {
            bool high = t % 500000u < duty * 5000u;
            scan(start + 5u + t, high ? mask : 0u);
            if (t >= 500000u) {
                for (uint c = 0; c < 8u; ++c) {
                    expect(c, 2u, duty);
                }
            }
        }
    }

    /* A stopped sampler must not turn its last HIGH sample into a valid static measurement. */
    now += 1100000u;
    levels = 0u;
    assert(!sw_mon_get(0, &state));
    assert(SW_MON_IS_UNSTABLE(state));

    /* Short LOW pulses entirely between scans are invisible, not a high-frequency reading. */
    start = now + 100u;
    for (uint t = 0u; t <= 1100000u; t += 10u) {
        scan(start + t, mask);
        now = start + t + 1u;
        levels = 0u;
        now = start + t + 2u;
        levels = mask;
    }
    expect(0, 0u, 100u);
    puts("software_monitor_test ok");
    return 0;
}
