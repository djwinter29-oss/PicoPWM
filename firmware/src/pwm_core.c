#include "pwm_core.h"

#include "control.h"
#include "pwmdriver/hw_pwm_driver.h"
#include "pwmdriver/sw_pwm_driver.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"

static volatile bool pwm_ready = false;

static void pwm_core_main(void) {
    hw_pwm_driver_init();
    sw_pwm_driver_init();

    pwm_ready = true;

    while (true) {
        control_process_pending();
        __wfi();
    }
}

void pwm_core_launch(void) {
    pwm_ready = false;
    multicore_launch_core1(pwm_core_main);
}

bool pwm_core_is_ready(void) {
    return pwm_ready;
}
