#include "pwmdriver/pwm_driver_internal.h"

#include <assert.h>
#include <stdint.h>

int main(void) {
    assert(PWM_DRIVER_STOPPED_FREQ_HZ == 0u);
    assert(PWM_DRIVER_STOPPED_DUTY_PERCENT == 0u);

    /* Stopping freezes the counter: a zero frequency adds no periods. */
    assert(pwm_driver_accumulate_pulse_count(10u, PWM_DRIVER_STOPPED_FREQ_HZ, 0u, 1000000u) == 10u);
    assert(pwm_driver_accumulate_pulse_count(10u, 1000u, 1000u, 1000u) == 10u);
    assert(pwm_driver_accumulate_pulse_count(10u, 1000u, 0u, 1000000u) == 1010u);
    assert(pwm_driver_accumulate_pulse_count(UINT32_MAX - 1u, 1000u, 0u, 1000000u) == UINT32_MAX);

    return 0;
}
