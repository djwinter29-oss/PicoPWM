#include "pwmdriver/generator/generator_math.h"

#include <assert.h>
#include <stddef.h>

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

    /* Sticky-high PIO loop: 1%, 50%, and 99% across short and long periods. */
    static const uint16_t periods[] = {1u, 10u, 100u, 1000u, 65535u};
    static const uint8_t duties[] = {1u, 50u, 99u};
    for (size_t period_index = 0u; period_index < sizeof(periods) / sizeof(periods[0]); ++period_index) {
        for (size_t duty_index = 0u; duty_index < sizeof(duties) / sizeof(duties[0]); ++duty_index) {
            pwm_pio_duty_program_t program;
            uint32_t simulated;
            uint32_t rounded_percent;

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