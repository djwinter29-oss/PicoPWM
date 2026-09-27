#include "pwmdriver/monitor/pio_monitor_decode.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
    uint32_t frequency;
    uint8_t duty;

    assert(!pio_monitor_decode_pair(0u, 0u, 150000000u, &frequency, &duty));
    assert(!pio_monitor_decode_pair(1u, 1u, 150000000u, NULL, &duty));
    assert(!pio_monitor_decode_pair(1u, 1u, 150000000u, &frequency, NULL));

    assert(pio_monitor_decode_pair(75u, 75u, 150000000u, &frequency, &duty));
    assert(frequency == 500000u);
    assert(duty == 50u);

    assert(pio_monitor_decode_pair(0u, 150u, 150000000u, &frequency, &duty));
    assert(frequency == 500000u);
    assert(duty == 0u);

    assert(pio_monitor_decode_pair(UINT32_MAX, UINT32_MAX, 150000000u, &frequency, &duty));
    assert(duty == 50u);

    return 0;
}