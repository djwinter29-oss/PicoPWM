/**
 * @file i2c_command_queue.h
 * @brief Fixed-depth queue for deferred commands in the PWM I2C protocol.
 */

#ifndef I2C_COMMAND_QUEUE_H
#define I2C_COMMAND_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/** @brief Maximum number of complete command writes retained by the protocol. */
#define I2C_COMMAND_QUEUE_DEPTH 4u
/** @brief Maximum command payload bytes. */
#define I2C_COMMAND_QUEUE_PAYLOAD 8u

/** @brief One PWM protocol command waiting for foreground execution. */
typedef struct {
    uint8_t reg;                               /**< Protocol command register. */
    uint8_t len;                               /**< Payload length, excluding the command register. */
    uint32_t epoch;                            /**< Attempt generation for stale completion rejection. */
    uint8_t payload[I2C_COMMAND_QUEUE_PAYLOAD]; /**< Command payload bytes. */
} i2c_command_slot_t;

/** @brief Ring of deferred PWM commands shared by the ISR producer and Core 0 consumer. */
typedef struct {
    i2c_command_slot_t slots[I2C_COMMAND_QUEUE_DEPTH]; /**< Storage for queued commands. */
    uint8_t head;                                       /**< Next slot written by the producer. */
    uint8_t tail;                                       /**< Next slot read by the consumer. */
    uint8_t count;                                      /**< Number of queued commands. */
} i2c_command_queue_t;

/**
 * @brief Queue one command. Returns false when the queue is already full.
 * @param epoch Attempt counter for this register. Completion may publish only when it is still current.
 */
static inline bool i2c_command_queue_push(i2c_command_queue_t *queue, uint8_t reg, const uint8_t *payload, uint8_t len,
                                          uint32_t epoch) {
    i2c_command_slot_t *slot;

    if (queue == NULL || queue->count >= I2C_COMMAND_QUEUE_DEPTH || len > I2C_COMMAND_QUEUE_PAYLOAD) {
        return false;
    }
    if (len > 0u && payload == NULL) {
        return false;
    }

    slot = &queue->slots[queue->head];
    slot->reg = reg;
    slot->len = len;
    slot->epoch = epoch;
    if (len > 0u) {
        memcpy(slot->payload, payload, len);
    }
    queue->head = (uint8_t)((queue->head + 1u) % I2C_COMMAND_QUEUE_DEPTH);
    queue->count++;
    return true;
}

/** @brief Remove the oldest command. Returns false when the queue is empty. */
static inline bool i2c_command_queue_pop(i2c_command_queue_t *queue, i2c_command_slot_t *out) {
    if (queue == NULL || out == NULL || queue->count == 0u) {
        return false;
    }

    *out = queue->slots[queue->tail];
    queue->tail = (uint8_t)((queue->tail + 1u) % I2C_COMMAND_QUEUE_DEPTH);
    queue->count--;
    return true;
}

#endif
