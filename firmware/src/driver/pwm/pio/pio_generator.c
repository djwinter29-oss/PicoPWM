/**
 * @file generator.c
 * @brief PIO generator backend implementation for the logical `pwm` layer.
 *
 * This module owns the PIO-backed PWM generator used for the logical PIO channels
 * exposed by `pwm`. Each backend-local channel is bound to one RP2040 PIO
 * state machine and programs a compact free-running PWM loop.
 *
 * The timing model is intentionally simple: one requested frequency is mapped to a
 * quantized PIO clock divider and loop period (`pio_math.h`), and the realized
 * frequency derived from that timing is published. Duty endpoints of
 * `0%` and `100%` (or zero frequency) are handled as static GPIO drive modes rather than by running
 * the PWM loop.
 *
 * Current limitations:
 * - Realized frequency is quantized by the available divider and loop-count space,
 *   so the applied frequency may differ slightly from the requested frequency.
 * - The backend implements continuous generation only; finite pulse bursts are not
 *   supported by this one-state-machine design.
 */

#include "pio_generator.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "../pwm_driver_internal.h"
#include "../pwm_math.h"
#include "pio_generator.pio.h"
#include "pio_math.h"

/** @brief Intended upper frequency limit for the PIO generator backend. */
#define PIO_GEN_MAX_FREQ_HZ 1000000u

/** @brief Runtime ownership and mode for one PIO generator channel. */
typedef struct {
    PIO pio;      /**< Owning PIO block for the channel. */
    uint8_t sm;   /**< Owning state machine index within @ref pio. */
    bool running; /**< True while the PIO loop owns the pin; false when it is a static SIO level. */
} pio_gen_channel_t;

/** @brief Per-channel PIO generator runtime ownership table. */
static pio_gen_channel_t pio_channels[PIO_PWM_DRIVER_COUNT];
/** @brief Cached system clock used by the PIO timing search. */
static uint32_t pio_gen_sys_clk_hz = 0u;
/** @brief Tracks whether the PIO generator program is already loaded per PIO block. */
static bool pio_program_loaded[2] = {false, false};
/** @brief Cached program offsets per PIO block for the PIO generator program. */
static uint8_t pio_program_offsets[2] = {0, 0};

/** @brief Publish the realized state for one backend-local channel into the shared driver cache. */
static void gen_publish_state(uint channel, uint32_t realized_freq_hz, uint8_t duty_percent) {
    pwm_driver_state_t state = {.freq_hz = realized_freq_hz, .duty = duty_percent};

    pwm_driver_store_applied_state((uint)PWM_BANK_B * PWM_DRIVER_CONFIG_BANK_SIZE + channel, &state);
}

/**
 * @brief Stop one PIO generator channel and drive its output to one static level.
 * @param channel Backend-local channel index.
 * @param high `true` to drive the pin high, `false` to drive it low.
 */
static void gen_drive_level(uint channel, bool high) {
    PIO pio = pio_channels[channel].pio;
    uint sm = pio_channels[channel].sm;
    uint pin = pio_pwm_config.channels[channel].gpio;

    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, high ? 1 : 0);
    pio_channels[channel].running = false;
}

/**
 * @brief Enable one PIO generator channel with the supplied realized timing.
 * @param channel Backend-local channel index.
 * @param period_count Backend loop count representing one PWM period.
 * @param clkdiv_x256 Quantized PIO clock divider in units of `1/256`.
 * @param duty_percent Requested duty in percent.
 */
static void gen_enable_channel(uint channel, uint16_t period_count, uint32_t clkdiv_x256, uint8_t duty_percent) {
    pio_gen_channel_t *ctx = &pio_channels[channel];
    PIO pio = ctx->pio;
    uint sm = ctx->sm;
    uint pin = pio_pwm_config.channels[channel].gpio;
    pwm_pio_duty_program_t program;

    pwm_pio_program_duty(period_count, duty_percent, &program);

    /* Static levels hand the pin to SIO, so only static -> PWM transitions must rebind it to PIO ownership. */
    if (!ctx->running) {
        pio_gpio_init(pio, pin);
        pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    }

    pio_sm_set_clkdiv_int_frac(pio, sm, (uint16_t)(clkdiv_x256 >> 8), (uint8_t)(clkdiv_x256 & 0xffu));
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);

    pio_sm_put_blocking(pio, sm, program.match);
    pio_sm_put_blocking(pio, sm, period_count);

    pio_sm_set_enabled(pio, sm, true);
    ctx->running = true;
}

/** @copydoc pio_gen_init */
bool pio_gen_init(void) {
    pio_gen_sys_clk_hz = clock_get_hz(clk_sys);

    for (uint i = 0u; i < 2u; i++) {
        PIO pio = i == 0u ? pio0 : pio1;

        if (!pio_program_loaded[i]) {
            pio_program_offsets[i] = pio_add_program(pio, &generator_program);
            pio_program_loaded[i] = true;
        }
    }

    for (uint i = 0u; i < PWM_DRIVER_CONFIG_BANK_SIZE; i++) {
        PIO pio = i < 4u ? pio0 : pio1;

        pio_channels[i].pio = pio;
        pio_channels[i].sm = (uint8_t)(i % 4u);

        generator_program_init(pio, pio_channels[i].sm, pio_program_offsets[pio == pio0 ? 0u : 1u],
                               pio_pwm_config.channels[i].gpio);
        gen_drive_level(i, false);
    }

    return true;
}

/** @copydoc pio_gen_set */
bool pio_gen_set(uint channel, uint32_t freq_hz, uint8_t duty) {
    uint16_t period_count = 0u;
    uint32_t clkdiv_x256 = 256u;
    uint8_t realized_duty;
    bool high;

    if (channel >= PWM_DRIVER_CONFIG_BANK_SIZE) {
        return false;
    }
    if (duty > 100u) {
        duty = 100u;
    }

    if (pwm_generator_resolve_static(freq_hz, duty, &high, &realized_duty)) {
        gen_drive_level(channel, high);
        gen_publish_state(channel, 0u, realized_duty);
        return true;
    }

    if (freq_hz > PIO_GEN_MAX_FREQ_HZ ||
        !pwm_pio_find_timing(pio_gen_sys_clk_hz, freq_hz, &period_count, &clkdiv_x256)) {
        return false;
    }

    gen_enable_channel(channel, period_count, clkdiv_x256, duty);
    gen_publish_state(channel, pwm_pio_realized_freq_hz(pio_gen_sys_clk_hz, period_count, clkdiv_x256), duty);
    return true;
}
