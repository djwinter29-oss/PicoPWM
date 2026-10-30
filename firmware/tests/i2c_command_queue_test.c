// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

#include "app/i2c/i2c_command_queue.h"

#include <assert.h>
#include <string.h>

int main(void) {
    i2c_command_queue_t queue = {0};
    i2c_command_slot_t slot;
    uint8_t payload[I2C_COMMAND_QUEUE_PAYLOAD];

    assert(!i2c_command_queue_pop(&queue, &slot));

    for (uint8_t index = 0u; index < I2C_COMMAND_QUEUE_DEPTH; ++index) {
        payload[0] = index;
        assert(i2c_command_queue_push(&queue, (uint8_t)(0x30u + index), payload, 1u, index + 1u));
    }
    assert(!i2c_command_queue_push(&queue, 0x90u, NULL, 0u, 1u));

    for (uint8_t index = 0u; index < I2C_COMMAND_QUEUE_DEPTH; ++index) {
        assert(i2c_command_queue_pop(&queue, &slot));
        assert(slot.reg == (uint8_t)(0x30u + index));
        assert(slot.len == 1u);
        assert(slot.payload[0] == index);
        assert(slot.epoch == (uint32_t)index + 1u);
    }
    assert(!i2c_command_queue_pop(&queue, &slot));
    assert(i2c_command_queue_push(&queue, 0x90u, NULL, 0u, 7u));
    assert(i2c_command_queue_pop(&queue, &slot));
    assert(slot.reg == 0x90u && slot.len == 0u);
    assert(slot.epoch == 7u);
    assert(!i2c_command_queue_push(&queue, 0x30u, NULL, 1u, 1u));
    assert(!i2c_command_queue_push(NULL, 0x30u, payload, 1u, 1u));

    memset(payload, 0x5a, sizeof(payload));
    assert(i2c_command_queue_push(&queue, 0x30u, payload, I2C_COMMAND_QUEUE_PAYLOAD, 9u));
    assert(i2c_command_queue_pop(&queue, &slot));
    assert(slot.len == I2C_COMMAND_QUEUE_PAYLOAD);
    assert(slot.epoch == 9u);
    assert(memcmp(slot.payload, payload, sizeof(payload)) == 0);

    return 0;
}
