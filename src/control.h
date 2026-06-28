#ifndef CONTROL_H
#define CONTROL_H

#include "pico/stdlib.h"
#include "hw_pwm.h"
#include "sw_pwm.h"

#define CONTROL_CHANNEL_COUNT (HW_PWM_COUNT + SW_PWM_COUNT)

// Logical channel IDs:
//   0..7   -> hardware PWM (8 channels, independent frequency)
//   8..23  -> software PWM (16 channels, < 1 kHz)

// ---- Command queue (Core 0 -> Core 1) ----

typedef enum {
    CTRL_CMD_SET_FREQ = 0,
    CTRL_CMD_SET_DUTY = 1,
    CTRL_CMD_STOP_ALL = 2,
} ctrl_cmd_type_t;

typedef struct {
    uint8_t  type;     // ctrl_cmd_type_t
    uint8_t  channel;  // logical channel 0..23
    float    freq;     // Hz (for SET_FREQ)
    float    duty;     // 0.0..1.0 (for SET_FREQ and SET_DUTY)
} ctrl_cmd_t;

// ---- Initialisation ----

// Initialise cached values and the command queue.
// Called on Core 0 before launching Core 1.
void control_init(void);

// ---- Setters (Core 0: non-blocking, push to queue) ----

bool control_set_freq(uint channel, float freq_hz);
bool control_set_duty(uint channel, float duty);

// ---- Getters (Core 0: direct reads, safe for cross-core) ----

float     control_get_freq(uint channel);
float     control_get_duty(uint channel);
uint32_t  control_get_pulse_count(uint channel);
bool      control_is_enabled(uint channel);

// ---- Queue processor (Core 1: called from pwm_core main loop) ----

// Drains the command queue and applies changes to PWM hardware.
void control_process_pending(void);

// ---- Stop / reset ----

// stop_all resets every channel to the power-up default:
//   freq = 0 Hz, duty = 50%, pulse_count = 0.
// Pushes a STOP_ALL command to the queue (processed by Core 1).
void control_stop_all(void);

#endif
