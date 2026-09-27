#include "i2c/i2c_irq_gate.h"
#include "i2c/i2c_write_queue.h"

#include <assert.h>

static void test_queue_reject_is_finished(void) {
    assert(i2c_write_queue_status(true) == I2C_WRITE_STATUS_QUEUED);
    assert(i2c_write_queue_status(false) == I2C_WRITE_STATUS_REJECTED);
    assert(I2C_WRITE_STATUS_REJECTED != I2C_WRITE_STATUS_QUEUED);
}

static void test_idle_keeps_tx_empty_masked(void) {
    i2c_irq_gate_t gate;

    i2c_irq_gate_init(&gate);
    assert(gate.irq_enabled);
    assert(!gate.tx_empty_unmasked);
}

static void test_unread_response_stretches_without_tx_empty(void) {
    i2c_irq_gate_t gate;
    i2c_gate_action_t action;

    i2c_irq_gate_init(&gate);
    action = i2c_irq_gate_on_read_request(&gate, false, true, 0u, 9u);
    assert(action == I2C_GATE_STRETCH);
    assert(!gate.irq_enabled);
    assert(!gate.tx_empty_unmasked);

    i2c_irq_gate_resume(&gate);
    assert(gate.irq_enabled);
    assert(!gate.tx_empty_unmasked);
}

static void test_multibyte_read_masks_tx_empty_after_last_byte(void) {
    i2c_irq_gate_t gate;
    i2c_gate_action_t action;
    uint8_t index = 0u;
    const uint8_t length = 9u;

    i2c_irq_gate_init(&gate);
    action = i2c_irq_gate_on_read_request(&gate, true, true, index, length);
    assert(action == I2C_GATE_WRITE_BYTE);
    assert(gate.tx_empty_unmasked);
    index++;

    while (index < length) {
        bool more_after_this = (uint8_t)(index + 1u) < length;

        action = i2c_irq_gate_on_tx_empty(&gate, true, index, length);
        assert(action == I2C_GATE_WRITE_BYTE);
        assert(gate.tx_empty_unmasked == more_after_this);
        index++;
    }

    assert(index == length);
    assert(!gate.tx_empty_unmasked);
    action = i2c_irq_gate_on_tx_empty(&gate, true, index, length);
    assert(action == I2C_GATE_MASK_TX);
    assert(!gate.tx_empty_unmasked);

    gate.tx_empty_unmasked = true;
    i2c_irq_gate_on_stop(&gate);
    assert(!gate.tx_empty_unmasked);
}

static void test_unsolicited_read_releases_the_clock(void) {
    i2c_irq_gate_t gate;
    i2c_gate_action_t action;

    i2c_irq_gate_init(&gate);
    action = i2c_irq_gate_on_read_request(&gate, false, false, 0u, 0u);
    assert(action == I2C_GATE_PAD_BYTE);
    assert(gate.irq_enabled);
    assert(!gate.tx_empty_unmasked);
}

static void test_one_byte_read_never_unmasks_tx_empty(void) {
    i2c_irq_gate_t gate;
    i2c_gate_action_t action;

    i2c_irq_gate_init(&gate);
    action = i2c_irq_gate_on_read_request(&gate, true, true, 0u, 1u);
    assert(action == I2C_GATE_WRITE_BYTE);
    assert(!gate.tx_empty_unmasked);

    action = i2c_irq_gate_on_read_request(&gate, true, true, 1u, 1u);
    assert(action == I2C_GATE_PAD_BYTE);
    assert(!gate.tx_empty_unmasked);
}

int main(void) {
    test_queue_reject_is_finished();
    test_idle_keeps_tx_empty_masked();
    test_unread_response_stretches_without_tx_empty();
    test_unsolicited_read_releases_the_clock();
    test_multibyte_read_masks_tx_empty_after_last_byte();
    test_one_byte_read_never_unmasks_tx_empty();
    return 0;
}
