/**
 * @file monitor.c
 * @brief Standalone Core 1 PIO PWM monitor backend prototype for the logical `pwmdriver` layer.
 *
 * This module measures the same GPIO bank used by the PIO generator backend, but it is
 * intentionally not wired into `pwm_driver.c` yet. Each backend-local channel owns one
 * PIO state machine that watches a single input pin and pushes one raw high-loop count
 * followed by one raw low-loop count for every completed PWM period.
 *
 * The current prototype favors a small program and simple software decode over exact edge
 * timestamping. Exported `freq_hz` and `duty` therefore reflect approximate measurements
 * derived from the dominant two-instruction loop body rather than a fully compensated edge
 * timing model.
 *
 * Current limitations:
 * - The module keeps only the latest DMA-written high/low sample pair. If software does not
 *   read often enough, intermediate PWM periods are intentionally discarded.
 * - The backend does not provide a reliable received pulse count and always reports
 *   `pulse_count = 0`.
 * - The DMA drain is intentionally a finite long-running transfer rather than a permanent
 *   self-rearming stream. With the current transfer count, monitoring eventually stops
 *   updating after roughly 36 minutes at 1 MHz input. Because initialization is one-shot,
 *   that exhausted state persists until reboot. After that point the backend preserves the
 *   last exported state and no longer applies the one-second static-level fallback.
 * - The software double-reads that two-word DMA snapshot and accepts it only when both reads
 *   match. That reduces torn-pair glitches, but it is still a heuristic rather than a strict
 *   coherence guarantee.
 * - The exported frequency and duty cycle are approximate because the conversion ignores
 *   fixed setup and branch overhead around each measured high/low segment.
 * - Inputs slower than 1 Hz are intentionally treated as permanent high or low levels
 *   rather than as in-spec PWM measurements.
 */

#include "monitor.h"

#include "pico/time.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/dma.h"

#include "../pwm_driver_internal.h"
#include "monitor.pio.h"

/** @brief Dominant PIO instruction cost for one measured high or low loop iteration. */
#define PIO_MON_LOOP_CYCLES 2u
/** @brief Word count in one DMA snapshot buffer; one coherent sample is high plus low. */
#define PIO_MON_DMA_BUFFER_WORDS 2u
/** @brief Byte-size ring selector used by the RP2040 DMA ring configuration. */
#define PIO_MON_DMA_RING_SIZE_BITS 3u
/** @brief Large one-shot DMA transfer count used to approximate continuous draining. */
#define PIO_MON_DMA_TRANSFER_COUNT UINT32_MAX
/** @brief Inactivity threshold used to treat a channel as a permanent level. */
#define PIO_MON_STATIC_TIMEOUT_US 1000000u

/* Latest-snapshot read policy. These thresholds are tuned together because they define when
 * two decoded samples are close enough to accept as one exported reading.
 */
/** @brief Maximum snapshot attempts before the monitor reports an unstable-read sentinel. */
#define PIO_MON_READ_POLICY_ATTEMPTS 5u
/** @brief Minimum allowed delta in decoded frequency between two acceptable snapshot reads. */
#define PIO_MON_READ_POLICY_FREQ_MIN_HZ 1u
/** @brief Allowed delta in decoded duty between two acceptable snapshot reads. */
#define PIO_MON_READ_POLICY_DUTY_CLOSE_PERCENT 1u

/** @brief Runtime ownership and last sample state for one PIO monitor channel. */
typedef struct {
    pwm_driver_state_t state; /**< Latest exported monitor state. */
    uint32_t published_pair_count; /**< Monotonic completed-pair count already reflected in @ref state. */
    uint64_t last_sample_us; /**< Timestamp of the most recent raw segment drained from DMA. */
    bool sample_valid; /**< Indicates whether one full PWM period has been captured. */

    PIO pio; /**< Owning PIO block for the channel. */
    int dma_channel; /**< Owning DMA channel that drains the PIO RX FIFO. */
    uint8_t sm; /**< Owning state machine index within @ref pio. */
    uint32_t dma_buffer[PIO_MON_DMA_BUFFER_WORDS]; /**< Circular DMA destination buffer that always holds the latest high/low pair. */
} pio_mon_channel_t;

/** @brief Per-channel standalone PIO monitor runtime ownership table. */
static pio_mon_channel_t pio_mon_channels[PIO_PWM_DRIVER_COUNT];
/** @brief Cached program offsets per PIO block for the PIO monitor program; `UINT8_MAX` means unloaded. */
static uint8_t pio_mon_program_offsets[2] = {UINT8_MAX, UINT8_MAX};
/** @brief Guards the standalone monitor lifecycle so init only runs once. */
static bool pio_mon_initialized = false;
/** @brief Cached system clock used by the monitor decode path. */
static uint32_t pio_mon_sys_clk_hz = 0u;
/** @brief Map a Pico SDK PIO instance to a dense local array index. */
static uint8_t pio_mon_index(PIO pio) {
    return pio == pio0 ? 0u : 1u;
}

/** @brief Return the owning PIO block for one backend-local monitor channel index. */
static PIO pio_mon_pio_for_channel(uint channel) {
    return channel < 4u ? pio0 : pio1;
}

/** @brief Return the owning state machine index for one backend-local monitor channel index. */
static uint8_t pio_mon_sm_for_channel(uint channel) {
    return (uint8_t)(channel % 4u);
}

/** @brief Start or restart one DMA channel so it continuously drains one PIO RX FIFO into its ring buffer. */
static void pio_mon_dma_arm(pio_mon_channel_t *ctx) {
    dma_channel_config config = dma_channel_get_default_config((uint)ctx->dma_channel);

    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_dreq(&config, pio_get_dreq(ctx->pio, ctx->sm, false));
    channel_config_set_ring(&config, true, PIO_MON_DMA_RING_SIZE_BITS);

    dma_channel_abort((uint)ctx->dma_channel);
    dma_channel_configure(
        (uint)ctx->dma_channel,
        &config,
        ctx->dma_buffer,
        &ctx->pio->rxf[ctx->sm],
        PIO_MON_DMA_TRANSFER_COUNT,
        true
    );
}

/* Latest-snapshot acceptance pipeline:
 * 1. read one best-effort stable raw pair from the DMA snapshot buffer
 * 2. compare two decoded results with a simple closeness policy
 * 3. publish either the decoded sample or the unstable sentinel
 */
/**
 * @brief Read the latest DMA snapshot twice and accept it only if both reads match.
 *
 * ponytail: This is a cheap coherence filter, not a hard lock-free publication protocol.
 * The ceiling is a rare torn pair when DMA updates the two-word buffer in the same window as
 * both software reads. In practice the DMA path is usually ahead of CPU observation and this
 * code also requires two similar decoded reads before accepting a sample, so the residual risk
 * is intentionally treated as low enough to ignore for this prototype. That tradeoff is
 * acceptable because the user has stated occasional glitches are fine and the monitor only
 * needs the latest approximate pair.
 * Treat this as an intentional best-effort snapshot, not as a defect to keep re-raising during
 * normal review unless the requirements change toward stricter measurement coherence.
 * If stricter coherence is needed later, upgrade to a ping-pong buffer or an explicit sequence
 * word that lets software reject partially published samples deterministically.
 */
static bool pio_mon_read_stable_pair(const pio_mon_channel_t *ctx, uint32_t *high_ticks, uint32_t *low_ticks) {
    uint32_t first_high;
    uint32_t first_low;
    uint32_t second_high;
    uint32_t second_low;

    first_high = ctx->dma_buffer[0];
    first_low = ctx->dma_buffer[1];
    second_high = ctx->dma_buffer[0];
    second_low = ctx->dma_buffer[1];
    if (first_high != second_high || first_low != second_low) {
        return false;
    }

    *high_ticks = second_high;
    *low_ticks = second_low;
    return true;
}

/** @brief Read the current DMA progress for one monitor channel as pair count plus exhaustion state. */
static void pio_mon_dma_status(const pio_mon_channel_t *ctx, uint32_t *pair_count, bool *dma_exhausted) {
    uint32_t transfer_count = dma_hw->ch[ctx->dma_channel].transfer_count;

    *pair_count = (PIO_MON_DMA_TRANSFER_COUNT - transfer_count) / PIO_MON_DMA_BUFFER_WORDS;
    *dma_exhausted = transfer_count == 0u;
}

/** @brief Return whether two decoded monitor values are close enough to treat as one reading. */
static bool pio_mon_values_close(uint32_t first_freq_hz, uint8_t first_duty, uint32_t second_freq_hz, uint8_t second_duty) {
    uint32_t freq_delta = first_freq_hz > second_freq_hz ? first_freq_hz - second_freq_hz : second_freq_hz - first_freq_hz;
    uint32_t reference_freq_hz = first_freq_hz > second_freq_hz ? first_freq_hz : second_freq_hz;
    uint32_t allowed_freq_delta = reference_freq_hz / 100u;
    uint8_t duty_delta = first_duty > second_duty ? (uint8_t)(first_duty - second_duty) : (uint8_t)(second_duty - first_duty);

    if (allowed_freq_delta < PIO_MON_READ_POLICY_FREQ_MIN_HZ) {
        allowed_freq_delta = PIO_MON_READ_POLICY_FREQ_MIN_HZ;
    }

    return freq_delta <= allowed_freq_delta && duty_delta <= PIO_MON_READ_POLICY_DUTY_CLOSE_PERCENT;
}

/** @brief Publish one exported monitor state and matching validity flag. */
static void pio_mon_publish_state(pio_mon_channel_t *ctx, uint32_t freq_hz, uint8_t duty, bool sample_valid) {
    ctx->state.freq_hz = freq_hz;
    ctx->state.duty = duty;
    ctx->state.pulse_count = 0u;
    ctx->sample_valid = sample_valid;
}

/** @brief Clear one channel's cached monitor sample and transient decode state. */
static void pio_mon_reset_channel(pio_mon_channel_t *ctx) {
    pio_mon_publish_state(ctx, 0u, 0u, false);
    ctx->published_pair_count = 0u;
    ctx->last_sample_us = time_us_64();
    ctx->dma_buffer[0] = 0u;
    ctx->dma_buffer[1] = 0u;
}

/** @brief Publish a permanent-high or permanent-low level after prolonged inactivity. */
static void pio_mon_publish_static_level(uint channel) {
    pio_mon_channel_t *ctx = &pio_mon_channels[channel];
    bool high = gpio_get(PWM_PIO_GPIO_PINS[channel]);

    pio_mon_publish_state(ctx, 0u, high ? 100u : 0u, true);
}

/** @brief Publish the defined unstable-read sentinel state. */
static void pio_mon_publish_unstable(pio_mon_channel_t *ctx) {
    pio_mon_publish_state(ctx, PIO_MON_UNSTABLE_FREQ_HZ, PIO_MON_UNSTABLE_DUTY, false);
}

/** @brief Convert one raw high/low sample pair into approximate exported frequency and duty values. */
static bool pio_mon_decode_pair(uint32_t high_ticks, uint32_t low_ticks, uint32_t *freq_hz, uint8_t *duty) {
    uint64_t total_ticks;
    uint64_t decode_denominator;
    uint64_t scaled_freq_hz;
    uint32_t duty_percent;

    total_ticks = (uint64_t)high_ticks + (uint64_t)low_ticks;
    if (total_ticks == 0u) {
        return false;
    }

    /* ponytail: This first monitor revision converts only the dominant two-cycle loop body.
     * The fixed edge-detect and push overhead is intentionally ignored to keep the PIO program
     * and decode path small for now. If high-frequency accuracy matters later, upgrade this to
     * a compensated timing model or edge timestamp design.
     */
    /* ponytail: The decode caches clk_sys at init time. The ceiling is runtime reclocking:
     * if firmware changes the system clock after pio_mon_init(), the exported frequency will
     * drift until the monitor is reinitialized. That tradeoff is acceptable now because the
     * current firmware sets the clock once at startup. If runtime reclocking is added later,
     * refresh this cached value or query the live clock here.
     */
    decode_denominator = (uint64_t)PIO_MON_LOOP_CYCLES * total_ticks;
    scaled_freq_hz = ((uint64_t)pio_mon_sys_clk_hz + (decode_denominator / 2u)) / decode_denominator;
    if (scaled_freq_hz > UINT32_MAX) {
        scaled_freq_hz = UINT32_MAX;
    }

    duty_percent = (uint32_t)(((uint64_t)high_ticks * 100u + (total_ticks / 2u)) / total_ticks);
    if (duty_percent > 100u) {
        duty_percent = 100u;
    }

    *freq_hz = (uint32_t)scaled_freq_hz;
    *duty = (uint8_t)duty_percent;
    return true;
}

/** @brief Retry snapshot reads until two decoded results are close enough or attempts are exhausted. */
static bool pio_mon_read_close_sample(const pio_mon_channel_t *ctx, uint32_t *freq_hz, uint8_t *duty) {
    bool have_previous = false;
    uint32_t previous_freq_hz = 0u;
    uint8_t previous_duty = 0u;

    for (uint attempt = 0; attempt < PIO_MON_READ_POLICY_ATTEMPTS; attempt++) {
        uint32_t high_ticks;
        uint32_t low_ticks;
        uint32_t candidate_freq_hz;
        uint8_t candidate_duty;

        if (!pio_mon_read_stable_pair(ctx, &high_ticks, &low_ticks)) {
            continue;
        }

        if (!pio_mon_decode_pair(high_ticks, low_ticks, &candidate_freq_hz, &candidate_duty)) {
            continue;
        }

        if (have_previous && pio_mon_values_close(previous_freq_hz, previous_duty, candidate_freq_hz, candidate_duty)) {
            *freq_hz = candidate_freq_hz;
            *duty = candidate_duty;
            return true;
        }

        previous_freq_hz = candidate_freq_hz;
        previous_duty = candidate_duty;
        have_previous = true;
    }

    return false;
}

/** @brief Drain the DMA ring and refresh one backend-local monitor channel state. */
static void pio_mon_refresh_channel(uint channel) {
    pio_mon_channel_t *ctx = &pio_mon_channels[channel];
    uint64_t now_us = time_us_64();
    uint32_t pair_count;
    bool dma_exhausted;

    pio_mon_dma_status(ctx, &pair_count, &dma_exhausted);

    if (pair_count != ctx->published_pair_count) {
        uint32_t freq_hz;
        uint8_t duty;

        ctx->last_sample_us = now_us;

        if (pio_mon_read_close_sample(ctx, &freq_hz, &duty)) {
            pio_mon_publish_state(ctx, freq_hz, duty, true);
        } else {
            pio_mon_publish_unstable(ctx);
        }

        ctx->published_pair_count = pair_count;
    }

    if (dma_exhausted) {
        return;
    }

    if ((now_us - ctx->last_sample_us) >= PIO_MON_STATIC_TIMEOUT_US) {
        pio_mon_publish_static_level(channel);
    }
}

/** @copydoc pio_mon_init */
void pio_mon_init(void) {
    if (pio_mon_initialized) {
        return;
    }

    pio_mon_sys_clk_hz = clock_get_hz(clk_sys);

    if (pio_mon_program_offsets[0] == UINT8_MAX) {
        pio_mon_program_offsets[0] = pio_add_program(pio0, &monitor_program);
    }

    if (pio_mon_program_offsets[1] == UINT8_MAX) {
        pio_mon_program_offsets[1] = pio_add_program(pio1, &monitor_program);
    }

    for (uint channel = 0; channel < PIO_PWM_DRIVER_COUNT; channel++) {
        pio_mon_channel_t *ctx = &pio_mon_channels[channel];
        uint pin = PWM_PIO_GPIO_PINS[channel];
        uint8_t program_offset;

        ctx->pio = pio_mon_pio_for_channel(channel);
        ctx->dma_channel = dma_claim_unused_channel(true);
        ctx->sm = pio_mon_sm_for_channel(channel);
        pio_mon_reset_channel(ctx);
        program_offset = pio_mon_program_offsets[pio_mon_index(ctx->pio)];

        monitor_program_init(ctx->pio, ctx->sm, program_offset, pin);
        pio_sm_set_enabled(ctx->pio, ctx->sm, false);
        pio_sm_clear_fifos(ctx->pio, ctx->sm);
        pio_sm_restart(ctx->pio, ctx->sm);
        pio_mon_dma_arm(ctx);
        pio_sm_set_enabled(ctx->pio, ctx->sm, true);
    }

    pio_mon_initialized = true;
}

/** @copydoc pio_mon_get */
bool pio_mon_get(uint channel, pwm_driver_state_t *state) {
    if (!pio_mon_initialized || channel >= PIO_PWM_DRIVER_COUNT || state == NULL) {
        return false;
    }

    pio_mon_refresh_channel(channel);
    *state = pio_mon_channels[channel].state;
    return pio_mon_channels[channel].sample_valid;
}