#ifndef CONTROL_H
#define CONTROL_H

#include "pico/stdlib.h"
#include "pwmdriver/pwm_driver.h"

#define CONTROL_CHANNEL_COUNT PWM_DRIVER_CHANNEL_COUNT

// Logical channel IDs:
//   0..7   -> hardware PWM
//   8..15  -> PIO PWM
//   16..23 -> software PWM

// ---- Initialisation ----

// Initialise the logical control layer.
// Called on Core 0 before launching Core 1.
void control_init(void);

// ---- Setters (Core 0: forwarded into the pwm_driver mailbox and applied on Core 1) ----

bool control_set_freq(uint channel, float freq_hz);
bool control_set_duty(uint channel, float duty);

// ---- Getters (Core 0: read the pwm_driver realized-state snapshot) ----

bool      control_get(uint channel, pwm_driver_state_t *state);

float     control_get_freq(uint channel);
float     control_get_duty(uint channel);
uint32_t  control_get_pulse_count(uint channel);
bool      control_is_enabled(uint channel);

// ---- Stop / reset ----

// stop_all disables every channel and restores default output settings:
//   freq = 0 Hz, duty = 50%.
// pulse_count remains read-only and monotonic from power-on.
// Each channel update is forwarded to Core 1 through the pwm_driver mailbox.
void control_stop_all(void);

#endif
