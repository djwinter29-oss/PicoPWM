/**
 * @file i2c_write_queue.h
 * @brief Fixed-depth queue of deferred I2C write commands.
 */

#ifndef I2C_WRITE_QUEUE_H
#define I2C_WRITE_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/** @brief Number of complete writes retained before further writes report busy. */
#define I2C_WRITE_QUEUE_DEPTH 4u
/** @brief Maximum payload bytes stored with one queued write. */
#define I2C_WRITE_QUEUE_PAYLOAD 8u

/** @brief One deferred I2C write captured from the slave ISR. */
typedef struct {
    uint8_t reg;                              /**< Register byte that selected the write. */
    uint8_t len;                              /**< Number of valid payload bytes. */
    uint8_t payload[I2C_WRITE_QUEUE_PAYLOAD]; /**< Register payload, excluding the register byte. */
} i2c_write_slot_t;

/** @brief Ring of deferred writes shared by the ISR producer and Core 0 consumer. */
typedef struct {
    i2c_write_slot_t slots[I2C_WRITE_QUEUE_DEPTH]; /**< Storage for queued writes. */
    uint8_t head;                                  /**< Next slot written by the producer. */
    uint8_t tail;                                  /**< Next slot read by the consumer. */
    uint8_t count;                                 /**< Number of queued writes. */
} i2c_write_queue_t;

/** @brief Queue one write. Returns false when the queue is already full. */
static inline bool i2c_write_queue_push(i2c_write_queue_t *queue, uint8_t reg, const uint8_t *payload, uint8_t len) {
    i2c_write_slot_t *slot;

    if (queue == NULL || queue->count >= I2C_WRITE_QUEUE_DEPTH || len > I2C_WRITE_QUEUE_PAYLOAD) {
        return false;
    }
    if (len > 0u && payload == NULL) {
        return false;
    }

    slot = &queue->slots[queue->head];
    slot->reg = reg;
    slot->len = len;
    if (len > 0u) {
        memcpy(slot->payload, payload, len);
    }
    queue->head = (uint8_t)((queue->head + 1u) % I2C_WRITE_QUEUE_DEPTH);
    queue->count++;
    return true;
}

/** @brief Status byte while a queued write is still waiting. Matches `PWM_DRIVER_RESULT_BUSY`. */
#define I2C_WRITE_STATUS_QUEUED 1u
/** @brief Finished status when a write is dropped because the queue is full. Matches `PWM_DRIVER_RESULT_UNAVAILABLE`.
 */
#define I2C_WRITE_STATUS_REJECTED 3u

/** @brief Status a master should observe for a write that was queued or rejected. */
static inline uint8_t i2c_write_queue_status(bool queued) {
    return queued ? I2C_WRITE_STATUS_QUEUED : I2C_WRITE_STATUS_REJECTED;
}

/** @brief Remove the oldest write. Returns false when the queue is empty. */
static inline bool i2c_write_queue_pop(i2c_write_queue_t *queue, i2c_write_slot_t *out) {
    if (queue == NULL || out == NULL || queue->count == 0u) {
        return false;
    }

    *out = queue->slots[queue->tail];
    queue->tail = (uint8_t)((queue->tail + 1u) % I2C_WRITE_QUEUE_DEPTH);
    queue->count--;
    return true;
}

#endif
