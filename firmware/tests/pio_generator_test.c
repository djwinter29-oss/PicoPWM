#include "driver/pwm/pio/pio_generator.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#include <assert.h>
#include <stdio.h>

PIO pio0;
PIO pio1;
bool pio_test_tx_fifo_full;
unsigned pio_test_puts;
bool pio_test_enabled[2][4];
uint pio_test_pc[2][4];
uint pio_test_fifo_count[2][4];
unsigned pio_test_starts;
int pio_test_gpio_level[32];

static pio_hw_t pio0_hw = {0};
static pio_hw_t pio1_hw = {1};

const pio_pwm_config_t pio_pwm_config = {.channels = {{0u}, {2u}, {4u}, {6u}, {8u}, {10u}, {12u}, {14u}}};

static uint32_t published_freq;
static uint8_t published_duty;
static uint published_channel = 99u;

void gpio_set_function(uint gpio, int function) {
    (void)gpio;
    (void)function;
}
void gpio_set_dir(uint gpio, int direction) {
    (void)gpio;
    (void)direction;
}
void gpio_put(uint gpio, int value) {
    assert(gpio < 32u);
    pio_test_gpio_level[gpio] = value;
}

void pwm_driver_store_applied_state(uint channel, const pwm_state_t *state) {
    published_channel = channel;
    published_freq = state->freq_hz;
    published_duty = state->duty;
}

int main(void) {
    pio0 = &pio0_hw;
    pio1 = &pio1_hw;
    assert(pio_gen_init());

    pio_test_tx_fifo_full = false;
    assert(pio_gen_set(0u, 1000u, 25u));
    assert(pio_test_puts >= 2u);
    assert(published_channel == 8u);
    assert(published_freq != 0u && published_duty != 0u);

    pio_test_tx_fifo_full = true;
    assert(!pio_gen_set(0u, 2000u, 40u));
    assert(published_freq == 0u && published_duty == 0u);
    assert(pio_test_gpio_level[0] == 0);
    pio_test_tx_fifo_full = false;
    assert(pio_gen_set(0u, 100000u, 50u));
    assert(published_freq != 0u);

    assert(!pio_gen_set(8u, 1000u, 50u));
    assert(!pio_gen_set(0u, 2000000u, 50u));
    assert(pio_gen_set(0u, 0u, 100u));
    assert(published_freq == 0u && published_duty == 100u);
    assert(pio_test_gpio_level[0] == 1);
    pio_test_tx_fifo_full = false;
    const uint32_t frequencies[] = {1000u, 100000u, 1000u, 1000000u, 100u};
    for (uint channel = 0; channel < 8u; ++channel) {
        for (uint i = 0; i < sizeof(frequencies) / sizeof(frequencies[0]); ++i) {
            unsigned before = pio_test_starts;
            assert(pio_gen_set(channel, frequencies[i], 50u));
            assert(pio_test_starts == before + 1u);
            assert(published_channel == channel + 8u && published_freq != 0u);
        }
        assert(pio_gen_set(channel, 0u, 0u));
        assert(pio_gen_set(channel, 100000u, 50u));
    }
    puts("pio_generator_test ok");
    return 0;
}
