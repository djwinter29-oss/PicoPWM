#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include "hw_pwm.h"
#include "sw_pwm.h"

#define CONTROL_CHANNEL_COUNT (HW_PWM_COUNT + SW_PWM_COUNT)

// Logical channel IDs:
//   0..7   -> hardware PWM (8 channels, independent frequency)
//   8..23  -> software PWM (16 channels, < 1 kHz)

void control_init(void);

bool control_set_freq(uint channel, float freq_hz);
bool control_set_duty(uint channel, float duty);

float control_get_freq(uint channel);
float control_get_duty(uint channel);
uint32_t control_get_pulse_count(uint channel);
bool control_is_enabled(uint channel);

// stop_all resets every channel to the power-up default:
//   freq = 0 Hz, duty = 50%, pulse_count = 0.
void control_stop_all(void);

#endif
