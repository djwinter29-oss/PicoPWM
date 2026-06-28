#include "control.h"
#include "hw_pwm.h"
#include "sw_pwm.h"
#include "pico/util/queue.h"

#define SW_CHANNEL_BASE HW_PWM_COUNT
#define CMD_QUEUE_LEN   32

// ---- Cached requested values (Core 0 only) ----

static float freqs[CONTROL_CHANNEL_COUNT];
static float duties[CONTROL_CHANNEL_COUNT];

// ---- Command queue (Core 0 writes, Core 1 reads) ----

static queue_t cmd_queue;

void control_init(void) {
    for (int i = 0; i < CONTROL_CHANNEL_COUNT; i++) {
        freqs[i] = 0.0f;
        duties[i] = 0.5f;
    }
    queue_init(&cmd_queue, sizeof(ctrl_cmd_t), CMD_QUEUE_LEN);
}

// ---- Setters (Core 0) ----

bool control_set_freq(uint channel, float freq_hz) {
    if (channel >= CONTROL_CHANNEL_COUNT) return false;
    if (freq_hz < 0.0f) freq_hz = 0.0f;

    freqs[channel] = freq_hz;

    ctrl_cmd_t cmd = {
        .type    = CTRL_CMD_SET_FREQ,
        .channel = (uint8_t)channel,
        .freq    = freq_hz,
        .duty    = duties[channel],
    };
    return queue_try_add(&cmd_queue, &cmd);
}

bool control_set_duty(uint channel, float duty) {
    if (channel >= CONTROL_CHANNEL_COUNT) return false;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    duties[channel] = duty;

    ctrl_cmd_t cmd = {
        .type    = CTRL_CMD_SET_DUTY,
        .channel = (uint8_t)channel,
        .freq    = 0.0f,
        .duty    = duty,
    };
    return queue_try_add(&cmd_queue, &cmd);
}

// ---- Getters (Core 0, safe for cross-core reads) ----

float control_get_freq(uint channel) {
    if (channel >= CONTROL_CHANNEL_COUNT) return 0.0f;
    return freqs[channel];
}

float control_get_duty(uint channel) {
    if (channel >= CONTROL_CHANNEL_COUNT) return 0.0f;
    return duties[channel];
}

uint32_t control_get_pulse_count(uint channel) {
    if (channel >= CONTROL_CHANNEL_COUNT) return 0;
    if (channel < HW_PWM_COUNT) {
        return hw_pwm_get_pulse_count(channel);
    } else {
        return sw_pwm_get_pulse_count(channel - SW_CHANNEL_BASE);
    }
}

bool control_is_enabled(uint channel) {
    if (channel >= CONTROL_CHANNEL_COUNT) return false;
    // Use cached freq so we don't need a cross-core read into hw/sw modules.
    return freqs[channel] > 0.0f;
}

// ---- Queue processor (Core 1) ----

void control_process_pending(void) {
    ctrl_cmd_t cmd;
    while (queue_try_remove(&cmd_queue, &cmd)) {
        switch (cmd.type) {
        case CTRL_CMD_SET_FREQ:
            if (cmd.channel < HW_PWM_COUNT) {
                hw_pwm_set_freq(cmd.channel, cmd.freq, cmd.duty);
            } else {
                sw_pwm_set_freq(cmd.channel - SW_CHANNEL_BASE, cmd.freq, cmd.duty);
            }
            break;

        case CTRL_CMD_SET_DUTY:
            if (cmd.channel < HW_PWM_COUNT) {
                hw_pwm_set_duty(cmd.channel, cmd.duty);
            } else {
                sw_pwm_set_duty(cmd.channel - SW_CHANNEL_BASE, cmd.duty);
            }
            break;

        case CTRL_CMD_STOP_ALL:
            for (int i = 0; i < CONTROL_CHANNEL_COUNT; i++) {
                if (i < HW_PWM_COUNT) {
                    hw_pwm_set_freq(i, 0.0f, 0.5f);
                    hw_pwm_set_pulse_count(i, 0);
                } else {
                    sw_pwm_set_freq(i - SW_CHANNEL_BASE, 0.0f, 0.5f);
                    sw_pwm_set_pulse_count(i - SW_CHANNEL_BASE, 0);
                }
            }
            break;
        }
    }
}

// ---- Stop / reset (Core 0) ----

void control_stop_all(void) {
    // Immediately reset cached values so getters return defaults right away.
    for (int i = 0; i < CONTROL_CHANNEL_COUNT; i++) {
        freqs[i] = 0.0f;
        duties[i] = 0.5f;
    }
    // Push the actual hardware reset to Core 1 via the queue.
    ctrl_cmd_t cmd = {
        .type    = CTRL_CMD_STOP_ALL,
        .channel = 0,
        .freq    = 0.0f,
        .duty    = 0.0f,
    };
    queue_try_add(&cmd_queue, &cmd);
}
