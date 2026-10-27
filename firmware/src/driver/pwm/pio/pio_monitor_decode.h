/**
 * @file pio_monitor_decode.h
 * @brief Pure PIO monitor high/low pair decoding helper.
 */

#ifndef PWMDRIVER_PIO_MONITOR_DECODE_H
#define PWMDRIVER_PIO_MONITOR_DECODE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/** @brief Dominant PIO instruction cost for one measured high or low loop iteration. */
#define PIO_MONITOR_DECODE_LOOP_CYCLES 2u

/**
 * @brief Convert one raw PIO high/low pair into approximate frequency and duty.
 * @param high_ticks Raw high-segment loop count.
 * @param low_ticks Raw low-segment loop count.
 * @param sys_clk_hz System clock used by the PIO state machine.
 * @param freq_hz_out Caller-owned realized frequency destination.
 * @param duty_out Caller-owned realized duty destination.
 * @return `false` when the pair contains no measured time or an output is absent.
 */
static inline bool pio_monitor_decode_pair(uint32_t high_ticks, uint32_t low_ticks, uint32_t sys_clk_hz,
                                           uint32_t *freq_hz_out, uint8_t *duty_out) {
    uint64_t total_ticks = (uint64_t)high_ticks + (uint64_t)low_ticks;
    uint64_t denominator;
    uint64_t frequency;
    uint32_t duty;

    if (total_ticks == 0u || freq_hz_out == NULL || duty_out == NULL) {
        return false;
    }

    denominator = (uint64_t)PIO_MONITOR_DECODE_LOOP_CYCLES * total_ticks;
    frequency = ((uint64_t)sys_clk_hz + (denominator / 2u)) / denominator;
    if (frequency > UINT32_MAX) {
        frequency = UINT32_MAX;
    }

    duty = (uint32_t)(((uint64_t)high_ticks * 100u + (total_ticks / 2u)) / total_ticks);
    if (duty > 100u) {
        duty = 100u;
    }

    *freq_hz_out = (uint32_t)frequency;
    *duty_out = (uint8_t)duty;
    return true;
}

#endif