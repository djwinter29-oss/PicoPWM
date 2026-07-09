#include "pwm_driver.h"

const uint PWM_HW_GPIO_PINS[HW_PWM_COUNT] = {
    0, 2, 4, 6, 8, 10, 12, 14
};

const uint PWM_SW_GPIO_PINS[SW_PWM_COUNT] = {
    1, 3, 5, 7, 9, 11, 13, 15,
    18, 19, 20, 21, 22, 25, 26, 27
};

const uint PWM_PIO_GPIO_PINS[PIO_PWM_DRIVER_COUNT] = {
    0, 2, 4, 6, 8, 10, 12, 14
};