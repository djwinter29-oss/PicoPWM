#include "driver/pwm/pwm_driver_internal.h"

#include <assert.h>

int main(void) {
    assert(PWM_DRIVER_STOPPED_FREQ_HZ == 0u);
    assert(PWM_DRIVER_STOPPED_DUTY_PERCENT == 0u);

    return 0;
}
