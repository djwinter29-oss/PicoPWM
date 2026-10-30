/**
 * @file i2c_slave_test.c
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#include "driver/i2c/i2c_slave.h"
#include "driver/i2c/i2c_slave_hw.h"

#include <assert.h>
#include <string.h>

i2c_inst_t i2c1_inst;
const i2c_board_config_t i2c_board_config = {0};

static void (*fake_isr)(void);
static uint8_t rx_fifo[128];
static bool rx_first_data_byte[128];
static size_t rx_head;
static size_t rx_tail;
static uint8_t tx_log[128];
static size_t tx_count;
static bool tx_fifo_full;
static bool tx_empty_irq;
static uint32_t latched_events;

void i2c_slave_hw_init(uint8_t address, void (*isr)(void)) {
    (void)address;
    fake_isr = isr;
    rx_head = rx_tail = tx_count = 0u;
    tx_fifo_full = tx_empty_irq = false;
    latched_events = 0u;
}

bool i2c_slave_hw_rx_pop(uint8_t *byte, bool *first_data_byte) {
    if (rx_head == rx_tail) {
        return false;
    }
    *byte = rx_fifo[rx_tail++];
    *first_data_byte = rx_first_data_byte[rx_tail - 1u];
    return true;
}

void i2c_slave_hw_tx_push(uint8_t byte) {
    assert(!tx_fifo_full);
    tx_log[tx_count++] = byte;
    tx_fifo_full = true;
}

uint32_t i2c_slave_hw_events(void) {
    return latched_events | ((tx_empty_irq && !tx_fifo_full) ? I2C_SLAVE_EVENT_TX_EMPTY : 0u);
}

void i2c_slave_hw_clear(uint32_t events) {
    latched_events &= ~events;
}

void i2c_slave_hw_set_tx_empty_irq(bool enabled) {
    tx_empty_irq = enabled;
}

static uint8_t last_frame[I2C_SLAVE_FRAME_CAPACITY];
static uint8_t last_frame_len;
static bool last_frame_overflow;
static unsigned write_calls;
static uint8_t frame_first_bytes[4];
static uint8_t frame_lengths[4];
static unsigned read_calls;
static uint8_t response[4] = {0x41u, 0x42u, 0x43u, 0x44u};
static uint8_t response_len = sizeof(response);

static void record_frame(const uint8_t *frame, uint8_t length, bool overflow) {
    if (write_calls < sizeof(frame_first_bytes)) {
        frame_first_bytes[write_calls] = length > 0u ? frame[0] : 0u;
        frame_lengths[write_calls] = length;
    }
    write_calls++;
    last_frame_len = length;
    last_frame_overflow = overflow;
    if (length > 0u) {
        memcpy(last_frame, frame, length);
    }
}

static bool build_response(uint8_t *out, uint8_t capacity, uint8_t *length) {
    read_calls++;
    if (capacity < response_len) {
        return false;
    }
    memcpy(out, response, response_len);
    *length = response_len;
    return true;
}

static const i2c_slave_ops_t callbacks = {
    .write_frame = record_frame,
    .read_response = build_response,
};

static void reset(void) {
    memset(last_frame, 0, sizeof(last_frame));
    last_frame_len = 0u;
    last_frame_overflow = false;
    write_calls = read_calls = 0u;
    memset(frame_first_bytes, 0, sizeof(frame_first_bytes));
    memset(frame_lengths, 0, sizeof(frame_lengths));
    response_len = sizeof(response);
    i2c_slave_init(0x40u, &callbacks);
}

static void queue_write(const uint8_t *bytes, size_t count) {
    assert(rx_head + count <= sizeof(rx_fifo));
    memcpy(&rx_fifo[rx_head], bytes, count);
    if (count > 0u) {
        rx_first_data_byte[rx_head] = true;
        memset(&rx_first_data_byte[rx_head + 1u], 0, count - 1u);
    }
    rx_head += count;
}

static void master_write(const uint8_t *bytes, size_t count, bool stop) {
    queue_write(bytes, count);
    fake_isr();
    if (stop) {
        latched_events |= I2C_SLAVE_EVENT_STOP;
        fake_isr();
    }
}

static void master_restart(void) {
    latched_events |= I2C_SLAVE_EVENT_RESTART;
    fake_isr();
}

static void master_read(uint8_t *out, size_t count, bool stop) {
    for (size_t index = 0u; index < count; ++index) {
        size_t before = tx_count;
        tx_fifo_full = false;
        if (index == 0u || !tx_empty_irq) {
            latched_events |= I2C_SLAVE_EVENT_RD_REQ;
        }
        fake_isr();
        assert(tx_count == before + 1u);
        out[index] = tx_log[tx_count - 1u];
    }
    if (stop) {
        latched_events |= I2C_SLAVE_EVENT_STOP;
        fake_isr();
    }
}

static void test_frames_are_opaque_and_wait_for_boundary(void) {
    const uint8_t frame[] = {0x30u, 0x01u, 0x02u, 0x03u};

    reset();
    master_write(frame, sizeof(frame), false);
    assert(write_calls == 0u);
    master_restart();
    assert(write_calls == 1u);
    assert(last_frame_len == sizeof(frame));
    assert(!last_frame_overflow);
    assert(memcmp(last_frame, frame, sizeof(frame)) == 0);
}

static void test_stop_delivers_frame_and_restart_splits_transactions(void) {
    const uint8_t first[] = {0xa0u, 0x01u};
    const uint8_t second[] = {0xb0u, 0x02u, 0x03u};

    reset();
    master_write(first, sizeof(first), true);
    master_write(second, sizeof(second), false);
    assert(write_calls == 1u);
    master_restart();
    assert(write_calls == 2u);
    assert(last_frame_len == sizeof(second));
    assert(memcmp(last_frame, second, sizeof(second)) == 0);
}

static void test_coalesced_receive_fifo_preserves_repeated_start_boundary(void) {
    const uint8_t first[] = {0xa0u, 0x11u};
    const uint8_t second[] = {0xb0u, 0x22u, 0x33u};

    reset();
    queue_write(first, sizeof(first));
    queue_write(second, sizeof(second));
    latched_events |= I2C_SLAVE_EVENT_RESTART;
    fake_isr();

    assert(write_calls == 2u);
    assert(frame_first_bytes[0] == first[0] && frame_lengths[0] == sizeof(first));
    assert(frame_first_bytes[1] == second[0] && frame_lengths[1] == sizeof(second));
}

static void test_oversized_frame_is_truncated_and_flagged(void) {
    uint8_t frame[I2C_SLAVE_FRAME_CAPACITY + 2u];

    for (uint8_t index = 0u; index < sizeof(frame); ++index) {
        frame[index] = index;
    }
    reset();
    master_write(frame, sizeof(frame), true);
    assert(write_calls == 1u);
    assert(last_frame_len == I2C_SLAVE_FRAME_CAPACITY);
    assert(last_frame_overflow);
    assert(memcmp(last_frame, frame, I2C_SLAVE_FRAME_CAPACITY) == 0);
}

static void test_read_response_streams_then_zero_pads_and_rebuilds(void) {
    uint8_t out[6];

    reset();
    master_read(out, sizeof(out), true);
    assert(read_calls == 1u);
    assert(memcmp(out, response, sizeof(response)) == 0);
    assert(out[4] == 0u && out[5] == 0u);
    assert(!tx_empty_irq);
    master_read(out, 2u, true);
    assert(read_calls == 2u);
    assert(out[0] == response[0] && out[1] == response[1]);
}

static void test_empty_response_is_zero_padded(void) {
    uint8_t out[3];

    reset();
    response_len = 0u;
    master_read(out, sizeof(out), true);
    assert(read_calls == 1u);
    assert(out[0] == 0u && out[1] == 0u && out[2] == 0u);
}

static void test_transmit_abort_cancels_response_and_allows_next_read(void) {
    uint8_t out[1];
    size_t sent;

    reset();
    master_read(out, sizeof(out), false);
    assert(out[0] == response[0]);
    assert(read_calls == 1u);
    assert(tx_empty_irq);

    latched_events |= I2C_SLAVE_EVENT_TX_ABORT;
    fake_isr();
    sent = tx_count;
    assert(!tx_empty_irq);

    fake_isr();
    assert(tx_count == sent);

    master_restart();
    master_read(out, sizeof(out), true);
    assert(read_calls == 2u);
    assert(out[0] == response[0]);
}

int main(void) {
    reset();
    assert(i2c_slave_address() == 0x40u);
    test_frames_are_opaque_and_wait_for_boundary();
    test_stop_delivers_frame_and_restart_splits_transactions();
    test_coalesced_receive_fifo_preserves_repeated_start_boundary();
    test_oversized_frame_is_truncated_and_flagged();
    test_read_response_streams_then_zero_pads_and_rebuilds();
    test_empty_response_is_zero_padded();
    test_transmit_abort_cancels_response_and_allows_next_read();
    return 0;
}
