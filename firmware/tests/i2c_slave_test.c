#include "driver/i2c/i2c_slave.h"
#include "driver/i2c/i2c_slave_hw.h"
#include "driver/i2c/i2c_write_queue.h"

#include <assert.h>
#include <string.h>

/* Fake peripheral: a byte FIFO each way plus latched STOP and RD_REQ events. */
i2c_inst_t i2c1_inst;
const i2c_board_config_t i2c_board_config = {0};

static void (*fake_isr)(void);
static uint8_t rx_fifo[64];
static size_t rx_head;
static size_t rx_tail;
static uint8_t tx_log[64];
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

bool i2c_slave_hw_rx_pop(uint8_t *byte) {
    if (rx_head == rx_tail) {
        return false;
    }
    *byte = rx_fifo[rx_tail++];
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

/* Fake register map: 0x10/0x20/0x21 are reads, 0x30/0x31/0x32 are writes, 0x33 is an over-long write. */
static unsigned read_calls;
static unsigned exec_calls;
static uint8_t exec_reg;
static uint8_t exec_payload[8];
static uint8_t exec_len;
static uint8_t exec_result;

static uint8_t map_write_length(uint8_t reg) {
    switch (reg) {
    case 0x10:
    case 0x20:
    case 0x21:
    case 0x31:
        return 1u;
    case 0x30:
        return 3u;
    case 0x32:
        return 9u;
    case 0x33:
        return 10u;
    default:
        return 0u;
    }
}

static bool map_is_write(uint8_t reg) {
    return reg >= 0x30u && reg <= 0x32u;
}

static bool map_read(uint8_t reg, uint8_t last_status, uint8_t *response, uint8_t capacity, uint8_t *length) {
    (void)capacity;
    read_calls++;
    if (reg == 0x10u) {
        const uint8_t value[] = {(uint8_t)read_calls, 2u, 3u, 4u, 5u};
        memcpy(response, value, sizeof(value));
        *length = sizeof(value);
        return true;
    }
    if (reg == 0x20u) {
        response[0] = 0xaau;
        *length = 1u;
        return true;
    }
    if (map_is_write(reg)) {
        response[0] = last_status;
        *length = 1u;
        return true;
    }
    return false;
}

static uint8_t map_execute(uint8_t reg, const uint8_t *payload, uint8_t length) {
    exec_calls++;
    exec_reg = reg;
    exec_len = length;
    memcpy(exec_payload, payload, length);
    return exec_result;
}

static const i2c_slave_ops_t map = {map_write_length, map_is_write, map_read, map_execute};

static void reset(void) {
    read_calls = exec_calls = 0u;
    exec_result = I2C_STATUS_OK;
    i2c_slave_init(0x40u, &map);
}

static void master_write(const uint8_t *bytes, size_t count, bool stop) {
    memcpy(&rx_fifo[rx_head], bytes, count);
    rx_head += count;
    fake_isr();
    if (stop) {
        latched_events |= I2C_SLAVE_EVENT_STOP;
        fake_isr();
    }
}

/* One master read of @p count bytes; the first byte raises RD_REQ, later ones follow TX_EMPTY when unmasked. */
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

/* A plain read returns the status of the last register written or selected. */
static uint8_t read_status(void) {
    uint8_t status;

    master_read(&status, 1u, true);
    return status;
}

static void test_read_register_pads_and_masks_tx_empty(void) {
    const uint8_t select = 0x10u;
    uint8_t out[7];

    reset();
    master_write(&select, 1u, true);
    assert(read_calls == 0u);
    master_read(out, 1u, false);
    assert(tx_empty_irq);
    master_read(&out[1], 3u, false);
    assert(tx_empty_irq);
    master_read(&out[4], 1u, false);
    assert(!tx_empty_irq);
    master_read(&out[5], 2u, true);
    assert(read_calls == 1u);
    assert(out[0] == 1u && out[1] == 2u && out[4] == 5u && out[5] == 0u && out[6] == 0u);
    assert(!tx_empty_irq);
}

static void test_every_read_transaction_rebuilds_the_response(void) {
    const uint8_t select = 0x10u;
    uint8_t out[5];

    reset();
    master_write(&select, 1u, true);
    master_read(out, 5u, true);
    assert(out[0] == 1u);
    master_read(out, 5u, true);
    assert(out[0] == 2u);
}

static void test_aborted_read_restarts_from_the_first_byte(void) {
    const uint8_t select = 0x10u;
    uint8_t out[5];

    reset();
    master_write(&select, 1u, true);
    master_read(out, 2u, true);
    assert(!tx_empty_irq);
    master_read(out, 5u, true);
    assert(out[0] == 2u && out[4] == 5u);
}

static void test_read_without_a_selected_register_returns_zero(void) {
    uint8_t out[2];

    reset();
    master_read(out, 2u, true);
    assert(out[0] == 0u && out[1] == 0u);
    assert(read_calls == 0u);
    assert(!tx_empty_irq);
}

static void test_unsupported_read_reports_invalid(void) {
    const uint8_t select = 0x21u;
    uint8_t out[2];

    reset();
    master_write(&select, 1u, true);
    master_read(out, 2u, true);
    assert(out[0] == I2C_STATUS_INVALID && out[1] == 0u);
}

static void test_restart_after_a_read_selects_the_new_register(void) {
    const uint8_t first = 0x10u;
    const uint8_t second = 0x20u;
    uint8_t out[2];

    reset();
    master_write(&first, 1u, false);
    master_read(out, 1u, false);
    master_write(&second, 1u, false);
    master_read(out, 1u, true);
    assert(out[0] == 0xaau);
}

static void test_command_write_is_queued_then_finished_by_poll(void) {
    const uint8_t command[] = {0x30u, 0xa1u, 0xb2u};

    reset();
    master_write(command, sizeof(command), true);
    assert(exec_calls == 0u);
    assert(read_status() == I2C_STATUS_QUEUED);
    i2c_slave_poll();
    assert(exec_calls == 1u && exec_reg == 0x30u && exec_len == 2u);
    assert(exec_payload[0] == 0xa1u && exec_payload[1] == 0xb2u);
    assert(read_status() == I2C_STATUS_OK);
    i2c_slave_poll();
    assert(exec_calls == 1u);
}

static void test_failed_command_publishes_its_status(void) {
    const uint8_t command[] = {0x31u};

    reset();
    exec_result = I2C_STATUS_INVALID;
    master_write(command, sizeof(command), true);
    i2c_slave_poll();
    assert(read_status() == I2C_STATUS_INVALID);
}

static void test_status_select_does_not_queue(void) {
    const uint8_t select = 0x30u;
    uint8_t status;

    reset();
    master_write(&select, 1u, false);
    master_read(&status, 1u, true);
    assert(status == I2C_STATUS_OK);
    master_write(&select, 1u, true);
    i2c_slave_poll();
    assert(exec_calls == 0u);
}

static void test_full_queue_reports_rejected(void) {
    const uint8_t command[] = {0x31u};

    reset();
    for (unsigned index = 0u; index < I2C_WRITE_QUEUE_DEPTH; ++index) {
        master_write(command, 1u, true);
        assert(read_status() == I2C_STATUS_QUEUED);
    }
    master_write(command, 1u, true);
    assert(read_status() == I2C_STATUS_REJECTED);
    for (unsigned index = 0u; index < I2C_WRITE_QUEUE_DEPTH; ++index) {
        i2c_slave_poll();
    }
    assert(exec_calls == I2C_WRITE_QUEUE_DEPTH);
}

static void test_stale_completion_does_not_replace_newer_status(void) {
    const uint8_t command[] = {0x31u};

    reset();
    master_write(command, 1u, true);
    master_write(command, 1u, true);
    exec_result = I2C_STATUS_INVALID;
    i2c_slave_poll();
    assert(read_status() == I2C_STATUS_QUEUED);
    exec_result = I2C_STATUS_OK;
    i2c_slave_poll();
    assert(read_status() == I2C_STATUS_OK);
}

static void test_malformed_command_drops_the_rest_of_the_transaction(void) {
    const uint8_t select = 0x20u;
    const uint8_t unknown_then_command[] = {0x77u, 0x31u};
    const uint8_t too_long[] = {0x33u, 0x31u};
    uint8_t out;

    reset();
    master_write(&select, 1u, true);
    master_write(unknown_then_command, sizeof(unknown_then_command), true);
    master_write(too_long, sizeof(too_long), true);
    i2c_slave_poll();
    assert(exec_calls == 0u);
    master_read(&out, 1u, true);
    assert(out == 0xaau);
}

static void test_largest_payload_is_accepted(void) {
    uint8_t command[9] = {0x32u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};

    reset();
    master_write(command, sizeof(command), true);
    i2c_slave_poll();
    assert(exec_calls == 1u && exec_len == 8u && exec_payload[7] == 8u);
}

int main(void) {
    assert(i2c_slave_address() == 0u);
    i2c_slave_poll();

    test_read_register_pads_and_masks_tx_empty();
    test_every_read_transaction_rebuilds_the_response();
    test_aborted_read_restarts_from_the_first_byte();
    test_read_without_a_selected_register_returns_zero();
    test_unsupported_read_reports_invalid();
    test_restart_after_a_read_selects_the_new_register();
    test_command_write_is_queued_then_finished_by_poll();
    test_failed_command_publishes_its_status();
    test_status_select_does_not_queue();
    test_full_queue_reports_rejected();
    test_stale_completion_does_not_replace_newer_status();
    test_malformed_command_drops_the_rest_of_the_transaction();
    test_largest_payload_is_accepted();
    assert(i2c_slave_address() == 0x40u);
    return 0;
}
