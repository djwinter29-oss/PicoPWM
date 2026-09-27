#include "i2c/i2c_write_queue.h"

#include <assert.h>
#include <string.h>

int main(void) {
    i2c_write_queue_t queue = {0};
    i2c_write_slot_t slot;
    uint8_t payload[I2C_WRITE_QUEUE_PAYLOAD];

    assert(!i2c_write_queue_pop(&queue, &slot));

    for (uint8_t index = 0u; index < I2C_WRITE_QUEUE_DEPTH; ++index) {
        payload[0] = index;
        assert(i2c_write_queue_push(&queue, (uint8_t)(0x30u + index), payload, 1u));
    }
    assert(!i2c_write_queue_push(&queue, 0x90u, NULL, 0u));

    for (uint8_t index = 0u; index < I2C_WRITE_QUEUE_DEPTH; ++index) {
        assert(i2c_write_queue_pop(&queue, &slot));
        assert(slot.reg == (uint8_t)(0x30u + index));
        assert(slot.len == 1u);
        assert(slot.payload[0] == index);
    }
    assert(!i2c_write_queue_pop(&queue, &slot));
    assert(i2c_write_queue_push(&queue, 0x90u, NULL, 0u));
    assert(i2c_write_queue_pop(&queue, &slot));
    assert(slot.reg == 0x90u && slot.len == 0u);
    assert(!i2c_write_queue_push(&queue, 0x30u, NULL, 1u));
    assert(!i2c_write_queue_push(NULL, 0x30u, payload, 1u));

    memset(payload, 0x5a, sizeof(payload));
    assert(i2c_write_queue_push(&queue, 0x30u, payload, I2C_WRITE_QUEUE_PAYLOAD));
    assert(i2c_write_queue_pop(&queue, &slot));
    assert(slot.len == I2C_WRITE_QUEUE_PAYLOAD);
    assert(memcmp(slot.payload, payload, sizeof(payload)) == 0);

    return 0;
}
