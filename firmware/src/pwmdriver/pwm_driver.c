#include "pwm_driver.h"
#include "pwm_driver_internal.h"

#include "hw_pwm_driver.h"
#include "pio_pwm_driver.h"
#include "sw_pwm_driver.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"

#include "hardware/sync.h"

#include <math.h>

#define PWM_DRIVER_CMD_QUEUE_LEN 32

const uint PWM_HW_GPIO_PINS[HW_PWM_COUNT] = {
    1, 3, 5, 7, 9, 11, 13, 15
};

const uint PWM_SW_GPIO_PINS[SW_PWM_COUNT] = {
    18, 19, 20, 21, 22, 25, 26, 27
};

const uint PWM_PIO_GPIO_PINS[PIO_PWM_DRIVER_COUNT] = {
    0, 2, 4, 6, 8, 10, 12, 14
};

static volatile bool pwm_ready = false;

typedef struct {
    uint8_t channel;
    float freq_hz;
    float duty;
} pwm_driver_cmd_t;

typedef struct {
    volatile uint32_t version;
    volatile float freq_hz;
    volatile float duty;
    volatile uint32_t pulse_count;
} pwm_driver_shared_state_t;

static queue_t pwm_cmd_queue;
static pwm_driver_shared_state_t pwm_state_cache[PWM_DRIVER_CHANNEL_COUNT];

static bool pwm_driver_is_hw_channel(uint channel) {
    return channel >= HW_PWM_CHANNEL_BASE && channel < PIO_PWM_CHANNEL_BASE;
}

static bool pwm_driver_is_pio_channel(uint channel) {
    return channel >= PIO_PWM_CHANNEL_BASE && channel < SW_PWM_CHANNEL_BASE;
}

static bool pwm_driver_is_sw_channel(uint channel) {
    return channel >= SW_PWM_CHANNEL_BASE && channel < PWM_DRIVER_CHANNEL_COUNT;
}

static void pwm_driver_cache_state(uint channel, const pwm_driver_state_t *state) {
    uint32_t irq_state;

    if (channel >= PWM_DRIVER_CHANNEL_COUNT || state == NULL) {
        return;
    }

    irq_state = save_and_disable_interrupts();
    pwm_state_cache[channel].version++;
    pwm_state_cache[channel].freq_hz = state->freq_hz;
    pwm_state_cache[channel].duty = state->duty;
    pwm_state_cache[channel].pulse_count = state->pulse_count;
    pwm_state_cache[channel].version++;
    restore_interrupts(irq_state);
}

void pwm_driver_store_applied_state(uint channel, const pwm_driver_state_t *state) {
    pwm_driver_cache_state(channel, state);
}

void pwm_driver_store_pulse_count(uint channel, uint32_t pulse_count) {
    uint32_t irq_state;

    if (channel >= PWM_DRIVER_CHANNEL_COUNT) {
        return;
    }

    irq_state = save_and_disable_interrupts();
    pwm_state_cache[channel].version++;
    pwm_state_cache[channel].pulse_count = pulse_count;
    pwm_state_cache[channel].version++;
    restore_interrupts(irq_state);
}

static bool pwm_driver_backend_set_freq(uint channel, float freq_hz, float duty) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT) {
        return false;
    }

    if (pwm_driver_is_hw_channel(channel)) {
        return hw_pwm_driver_set_freq(channel - HW_PWM_CHANNEL_BASE, freq_hz, duty);
    }

    if (pwm_driver_is_pio_channel(channel)) {
        return pio_pwm_driver_set_freq(channel - PIO_PWM_CHANNEL_BASE, freq_hz, duty);
    }

    return sw_pwm_driver_set_freq(channel - SW_PWM_CHANNEL_BASE, freq_hz, duty);
}

static bool pwm_driver_backend_get(uint channel, pwm_driver_state_t *state) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT || state == NULL) {
        return false;
    }

    if (pwm_driver_is_hw_channel(channel)) {
        return hw_pwm_driver_get(channel - HW_PWM_CHANNEL_BASE, state);
    }

    if (pwm_driver_is_pio_channel(channel)) {
        return pio_pwm_driver_get(channel - PIO_PWM_CHANNEL_BASE, state);
    }

    return sw_pwm_driver_get(channel - SW_PWM_CHANNEL_BASE, state);
}

static void pwm_driver_cache_defaults(void) {
    for (uint channel = 0; channel < PWM_DRIVER_CHANNEL_COUNT; channel++) {
        pwm_driver_state_t state = {
            .freq_hz = 0.0f,
            .duty = 0.5f,
            .pulse_count = 0,
        };

        pwm_state_cache[channel].version = 0;
        pwm_driver_cache_state(channel, &state);
    }
}

static void pwm_driver_process_mailbox(void) {
    pwm_driver_cmd_t cmd;

    while (queue_try_remove(&pwm_cmd_queue, &cmd)) {
        pwm_driver_backend_set_freq(cmd.channel, cmd.freq_hz, cmd.duty);
    }
}

static void pwm_driver_core_main(void) {
    hw_pwm_driver_init();
    pio_pwm_driver_init();
    sw_pwm_driver_init();

    pwm_ready = true;

    while (true) {
        pwm_driver_process_mailbox();
        __wfe();
    }
}

void pwm_driver_launch(void) {
    pwm_ready = false;
    queue_init(&pwm_cmd_queue, sizeof(pwm_driver_cmd_t), PWM_DRIVER_CMD_QUEUE_LEN);
    pwm_driver_cache_defaults();
    multicore_launch_core1(pwm_driver_core_main);
}

bool pwm_driver_is_ready(void) {
    return pwm_ready;
}

bool pwm_driver_set_freq(uint channel, float freq_hz, float duty) {
    if (channel >= PWM_DRIVER_CHANNEL_COUNT || !isfinite(freq_hz) || !isfinite(duty)) {
        return false;
    }

    if (get_core_num() != 0) {
        return pwm_driver_backend_set_freq(channel, freq_hz, duty);
    }

    if (duty < 0.0f) {
        duty = 0.0f;
    }
    if (duty > 1.0f) {
        duty = 1.0f;
    }

    pwm_driver_cmd_t cmd = {
        .channel = (uint8_t)channel,
        .freq_hz = freq_hz,
        .duty = duty,
    };

    if (!queue_try_add(&pwm_cmd_queue, &cmd)) {
        return false;
    }

    __sev();
    return true;
}

bool pwm_driver_get(uint channel, pwm_driver_state_t *state) {
    uint32_t version_before;
    uint32_t version_after;

    if (channel >= PWM_DRIVER_CHANNEL_COUNT || state == NULL) {
        return false;
    }

    if (get_core_num() != 0) {
        return pwm_driver_backend_get(channel, state);
    }

    do {
        version_before = pwm_state_cache[channel].version;
        if (version_before & 1u) {
            continue;
        }

        state->freq_hz = pwm_state_cache[channel].freq_hz;
        state->duty = pwm_state_cache[channel].duty;
        state->pulse_count = pwm_state_cache[channel].pulse_count;
        version_after = pwm_state_cache[channel].version;
    } while ((version_before != version_after) || (version_after & 1u));

    return true;
}