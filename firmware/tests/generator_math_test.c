#include "pwmdriver/generator/generator_math.h"

#include <assert.h>

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

    return 0;
}