#include "pwmdriver/pwm_driver_internal.h"

#include <assert.h>

int main(void) {
    assert(pwm_driver_timeout_disposition(PWM_DRIVER_MAILBOX_PENDING) == PWM_DRIVER_TIMEOUT_CANCEL);
    assert(pwm_driver_timeout_disposition(PWM_DRIVER_MAILBOX_COMPLETE) == PWM_DRIVER_TIMEOUT_TAKE_REPLY);
    assert(pwm_driver_timeout_disposition(PWM_DRIVER_MAILBOX_ACTIVE) == PWM_DRIVER_TIMEOUT_IN_FLIGHT);
    assert(pwm_driver_timeout_disposition(PWM_DRIVER_MAILBOX_IDLE) == PWM_DRIVER_TIMEOUT_IN_FLIGHT);
    return 0;
}
