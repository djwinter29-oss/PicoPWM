#include "driver/pwm/hardware/hardware_math.h"
#include "driver/pwm/pio/pio_math.h"
#include "driver/pwm/pwm_math.h"

#include <assert.h>
#include <stddef.h>

/** @brief Count high iterations of the PIO generator's sticky-high countdown. */
static uint32_t pwm_pio_simulate_high_iterations(uint16_t period_count, uint32_t match) {
    uint32_t high_iterations = 0u;
    bool pin_high = false;
    uint32_t y = period_count;

    while (true) {
        if (y == match) {
            pin_high = true;
        }
        if (pin_high) {
            high_iterations++;
        }
        if (y == 0u) {
            break;
        }
        y--;
    }

    return high_iterations;
}

/** @brief Sweep the PIO timing search and return the worst relative error in ppm; asserts it is realizable. */
static uint32_t pwm_pio_sweep_worst_ppm(uint32_t sys_clk_hz, uint32_t min_freq_hz) {
    uint32_t worst_ppm = 0u;

    for (uint64_t freq = min_freq_hz; freq <= 1000000u; freq += freq / 50u + 1u) {
        uint16_t period = 0u;
        uint32_t clkdiv = 256u;
        uint32_t realized;
        uint32_t error;
        uint32_t ppm;

        assert(pwm_pio_find_timing(sys_clk_hz, (uint32_t)freq, &period, &clkdiv));
        assert(period >= 1u && clkdiv >= 256u && clkdiv <= PWM_PIO_MAX_CLKDIV_X256);
        realized = pwm_pio_realized_freq_hz(sys_clk_hz, period, clkdiv);
        error = realized > freq ? (uint32_t)(realized - freq) : (uint32_t)(freq - realized);
        ppm = (uint32_t)((uint64_t)error * 1000000u / freq);
        if (ppm > worst_ppm) {
            worst_ppm = ppm;
        }
    }

    return worst_ppm;
}

int main(void) {
    bool high;
    uint8_t realized_duty;

    assert(pwm_generator_level_from_duty(100u, 0u) == 0u);
    assert(pwm_generator_level_from_duty(100u, 50u) == 50u);
    assert(pwm_generator_level_from_duty(100u, 100u) == 100u);
    assert(pwm_generator_level_from_duty(100u, 255u) == 100u);
    assert(pwm_generator_level_from_duty(3u, 50u) == 2u);
    assert(pwm_generator_level_from_duty(UINT32_MAX, 100u) == UINT32_MAX);

    assert(pwm_generator_resolve_static(0u, 0u, &high, &realized_duty));
    assert(!high && realized_duty == 0u);
    assert(pwm_generator_resolve_static(0u, 100u, &high, &realized_duty));
    assert(high && realized_duty == 100u);
    assert(pwm_generator_resolve_static(100u, 0u, &high, &realized_duty));
    assert(!high && realized_duty == 0u);
    assert(pwm_generator_resolve_static(100u, 100u, &high, &realized_duty));
    assert(high && realized_duty == 100u);
    assert(!pwm_generator_resolve_static(100u, 50u, &high, &realized_duty));
    assert(!pwm_generator_resolve_static(100u, 50u, NULL, &realized_duty));

    {
        static const uint32_t clocks[] = {125000000u, 150000000u, 250000000u};
        uint16_t period;
        uint32_t clkdiv;

        for (size_t i = 0u; i < sizeof(clocks) / sizeof(clocks[0]); ++i) {
            /* Divider quantization (1/256 near a divider of 1) bounds the error at about 0.2%. */
            assert(pwm_pio_sweep_worst_ppm(clocks[i], 1u) < 4000u);
        }
        assert(!pwm_pio_find_timing(125000000u, 0u, &period, &clkdiv));
        assert(!pwm_pio_find_timing(125000000u, 60000000u, &period, &clkdiv));
    }

    /* Sticky-high PIO loop: 1%, 50%, and 99% across short and long periods. */
    static const uint16_t periods[] = {1u, 10u, 100u, 1000u, 65535u};
    static const uint8_t duties[] = {1u, 50u, 99u};
    for (size_t period_index = 0u; period_index < sizeof(periods) / sizeof(periods[0]); ++period_index) {
        for (size_t duty_index = 0u; duty_index < sizeof(duties) / sizeof(duties[0]); ++duty_index) {
            pwm_pio_duty_program_t program;
            uint32_t simulated;
            uint32_t rounded_percent;

            pwm_pio_program_duty(periods[period_index], duties[duty_index], NULL);
            pwm_pio_program_duty(periods[period_index], duties[duty_index], &program);
            simulated = pwm_pio_simulate_high_iterations(periods[period_index], program.match);
            assert(simulated == program.high_iterations);
            assert(program.high_iterations <= program.total_iterations);
            /* Short periods quantize percent coarsely. Longer periods stay within 1%. */
            if (program.total_iterations >= 100u) {
                rounded_percent =
                    (uint32_t)(((uint64_t)program.high_iterations * 100u + (program.total_iterations / 2u)) /
                               program.total_iterations);
                assert(rounded_percent + 1u >= duties[duty_index] && duties[duty_index] + 1u >= rounded_percent);
            }
        }
    }

    return 0;
}