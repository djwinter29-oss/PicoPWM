#ifndef TEST_HARDWARE_PWM_H
#define TEST_HARDWARE_PWM_H

enum { PWM_CHAN_A = 0u, PWM_CHAN_B = 1u };

static inline unsigned int pwm_gpio_to_channel(unsigned int gpio) {
    return gpio & 1u;
}

#endif
