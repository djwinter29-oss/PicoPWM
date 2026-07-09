#include "control.h"
#include "hardware/clocks.h"

#include <math.h>

static pwm_driver_state_t control_get_state_or_default(uint channel) {
    pwm_driver_state_t state = {
        .freq_hz = 0.0f,
        .duty = 0.5f,
        .pulse_count = 0,
    };

    if (channel >= CONTROL_CHANNEL_COUNT) {
        return state;
    }

    pwm_driver_get(channel, &state);
    return state;
}

bool control_get(uint channel, pwm_driver_state_t *state) {
    if (channel >= CONTROL_CHANNEL_COUNT || state == NULL) {
        return false;
    }

    return pwm_driver_get(channel, state);
}

static bool freq_supported_for_channel(uint channel, float freq_hz) {
    if (!isfinite(freq_hz)) {
        return false;
    }

    if (freq_hz < 0.0f) {
        return false;
    }

    if (freq_hz == 0.0f) {
        return true;
    }

    if (channel < PIO_PWM_CHANNEL_BASE) {
        const float sys_clk = (float)clock_get_hz(clk_sys);
        const float min_hw = sys_clk / ((255.0f + 15.0f / 16.0f) * 65536.0f);
        const float max_hw = sys_clk / 2.0f;
        return freq_hz >= min_hw && freq_hz <= max_hw;
    }

    if (channel < SW_PWM_CHANNEL_BASE) {
        return freq_hz <= 100000.0f;
    }

    return freq_hz <= 100000.0f;
}

void control_init(void) {
    // No local shadow state is required; pwm_driver owns the shared snapshot.
}

// ---- Setters (Core 0) ----

bool control_set_freq(uint channel, float freq_hz) {
    pwm_driver_state_t state;

    if (channel >= CONTROL_CHANNEL_COUNT) return false;
    if (!freq_supported_for_channel(channel, freq_hz)) return false;

    if (freq_hz < 0.0f) freq_hz = 0.0f;

    state = control_get_state_or_default(channel);

    return pwm_driver_set_freq(channel, freq_hz, state.duty);
}

bool control_set_duty(uint channel, float duty) {
    pwm_driver_state_t state;

    if (channel >= CONTROL_CHANNEL_COUNT) return false;
    if (!isfinite(duty)) return false;
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    state = control_get_state_or_default(channel);

    return pwm_driver_set_freq(channel, state.freq_hz, duty);
}

// ---- Getters (Core 0, safe for cross-core reads) ----

float control_get_freq(uint channel) {
    return control_get_state_or_default(channel).freq_hz;
}

float control_get_duty(uint channel) {
    return control_get_state_or_default(channel).duty;
}

uint32_t control_get_pulse_count(uint channel) {
    pwm_driver_state_t state = {0};

    if (channel >= CONTROL_CHANNEL_COUNT) return 0;
    if (!control_get(channel, &state)) return 0;

    return state.pulse_count;
}

bool control_is_enabled(uint channel) {
    return control_get_state_or_default(channel).freq_hz > 0.0f;
}

// ---- Stop / reset (Core 0) ----

void control_stop_all(void) {
    for (int i = 0; i < CONTROL_CHANNEL_COUNT; i++) {
        pwm_driver_set_freq(i, 0.0f, 0.5f);
    }
}
