#include "pwmdriver/pwm_driver_mailbox.h"

#include <assert.h>

int main(void) {
    assert(pwm_mailbox_admits_submit(PWM_DRIVER_MAILBOX_IDLE));
    assert(pwm_mailbox_admits_submit(PWM_DRIVER_MAILBOX_COMPLETE));
    assert(!pwm_mailbox_admits_submit(PWM_DRIVER_MAILBOX_PENDING));
    assert(!pwm_mailbox_admits_submit(PWM_DRIVER_MAILBOX_ACTIVE));

    assert(pwm_mailbox_timeout_action(PWM_DRIVER_MAILBOX_PENDING) == PWM_MAILBOX_TIMEOUT_CANCEL);
    assert(pwm_mailbox_timeout_action(PWM_DRIVER_MAILBOX_COMPLETE) == PWM_MAILBOX_TIMEOUT_TAKE);
    assert(pwm_mailbox_timeout_action(PWM_DRIVER_MAILBOX_ACTIVE) == PWM_MAILBOX_TIMEOUT_WAIT);
    assert(pwm_mailbox_timeout_action(PWM_DRIVER_MAILBOX_IDLE) == PWM_MAILBOX_TIMEOUT_WAIT);

    return 0;
}
