/**
 * @file usb_cdc.c
 * @brief TinyUSB CDC transport helpers for the PicoPWM CLI.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#include "app/usb/usb_cdc.h"

#include "bsp/board_api.h"
#include "tusb.h"

#include <string.h>

/** @brief TinyUSB CDC interface index used by the CLI transport. */
#define USB_CDC_ITF 0u
/** @brief Maximum bytes read per TinyUSB CDC receive callback chunk. */
#define USB_CDC_PACKET_SIZE 64u
/** @brief Local receive queue capacity in bytes. */
#define USB_CDC_RX_QUEUE_SIZE 128u
/** @brief Local transmit queue capacity in bytes. */
#define USB_CDC_TX_QUEUE_SIZE 2048u

/**
 * Receive and transmit queues are touched only on Core 0. TinyUSB callbacks run
 * inside tud_task() from usb_cdc_poll(), the same context as usb_cdc_read() and
 * usb_cdc_write(). Do not move TinyUSB onto the USB IRQ without making these
 * indexes interrupt-safe.
 */
/** @brief Circular receive queue storage. */
static uint8_t usb_cdc_rx_queue[USB_CDC_RX_QUEUE_SIZE];
/** @brief Head index for the receive queue. */
static uint32_t usb_cdc_rx_head;
/** @brief Tail index for the receive queue. */
static uint32_t usb_cdc_rx_tail;
/** @brief Number of bytes currently stored in the receive queue. */
static uint32_t usb_cdc_rx_count;
/** @brief Circular transmit queue storage. */
static uint8_t usb_cdc_tx_queue[USB_CDC_TX_QUEUE_SIZE];
/** @brief Head index for the transmit queue. */
static uint32_t usb_cdc_tx_head;
/** @brief Tail index for the transmit queue. */
static uint32_t usb_cdc_tx_tail;
/** @brief Number of bytes currently stored in the transmit queue. */
static uint32_t usb_cdc_tx_count;
/** @brief Number of DTR disconnect transitions observed by TinyUSB. */
static uint32_t usb_cdc_disconnects;
/** @brief Disconnect count whose application-level shell reset is complete. */
static uint32_t usb_cdc_handled_disconnects;
/** @brief Most recently observed host DTR state. */
static bool usb_cdc_dtr;

/** @brief Check whether the current CDC session may exchange transport data. */
static bool usb_cdc_session_io_allowed(void) {
    return usb_cdc_is_connected() && (usb_cdc_disconnects == usb_cdc_handled_disconnects);
}

/** @brief Queue one received CDC byte when local receive space remains. */
static void usb_cdc_queue_byte(uint8_t byte) {
    if (usb_cdc_rx_count >= USB_CDC_RX_QUEUE_SIZE) {
        return;
    }

    usb_cdc_rx_queue[usb_cdc_rx_head] = byte;
    usb_cdc_rx_head = (usb_cdc_rx_head + 1u) % USB_CDC_RX_QUEUE_SIZE;
    usb_cdc_rx_count += 1u;
}

/**
 * @brief Move received CDC bytes into the local queue, leaving the rest in the USB FIFO.
 * @note A full local queue stops the read so the host is NAKed instead of having bytes discarded.
 */
static void usb_cdc_drain_rx(void) {
    uint8_t buffer[USB_CDC_PACKET_SIZE];

    while (usb_cdc_rx_count < USB_CDC_RX_QUEUE_SIZE && tud_cdc_n_available(USB_CDC_ITF) != 0u) {
        uint32_t space = USB_CDC_RX_QUEUE_SIZE - usb_cdc_rx_count;
        uint32_t want = space < sizeof(buffer) ? space : (uint32_t)sizeof(buffer);
        uint32_t count = tud_cdc_n_read(USB_CDC_ITF, buffer, want);

        if (count == 0u) {
            break;
        }
        for (uint32_t index = 0u; index < count; ++index) {
            usb_cdc_queue_byte(buffer[index]);
        }
    }
}

/**
 * @brief Append bytes to the local CDC transmit queue.
 * @param data Caller-owned payload bytes.
 * @param length Number of bytes to queue.
 * @return `true` when all bytes were queued, otherwise `false`.
 */
static bool usb_cdc_tx_queue_push(const uint8_t *data, uint32_t length) {
    if ((data == NULL) || (length == 0u)) {
        return false;
    }

    if ((USB_CDC_TX_QUEUE_SIZE - usb_cdc_tx_count) < length) {
        return false;
    }

    for (uint32_t index = 0u; index < length; ++index) {
        usb_cdc_tx_queue[usb_cdc_tx_head] = data[index];
        usb_cdc_tx_head = (usb_cdc_tx_head + 1u) % USB_CDC_TX_QUEUE_SIZE;
        usb_cdc_tx_count += 1u;
    }

    return true;
}

/** @brief Flush queued transmit data into the TinyUSB CDC endpoint when possible. */
static void usb_cdc_poll_tx(void) {
    bool flushed = false;

    if (!usb_cdc_session_io_allowed()) {
        return;
    }

    while (usb_cdc_tx_count != 0u) {
        uint32_t available = tud_cdc_n_write_available(USB_CDC_ITF);
        uint32_t contiguous = USB_CDC_TX_QUEUE_SIZE - usb_cdc_tx_tail;
        uint32_t chunk = usb_cdc_tx_count;
        uint32_t written;

        if (available == 0u) {
            break;
        }

        if (chunk > contiguous) {
            chunk = contiguous;
        }

        if (chunk > available) {
            chunk = available;
        }

        written = tud_cdc_n_write(USB_CDC_ITF, &usb_cdc_tx_queue[usb_cdc_tx_tail], chunk);
        if (written == 0u) {
            break;
        }

        usb_cdc_tx_tail = (usb_cdc_tx_tail + written) % USB_CDC_TX_QUEUE_SIZE;
        usb_cdc_tx_count -= written;
        flushed = true;
    }

    if (flushed) {
        tud_cdc_n_write_flush(USB_CDC_ITF);
    }
}

void usb_cdc_init(void) {
    usb_cdc_rx_head = 0u;
    usb_cdc_rx_tail = 0u;
    usb_cdc_rx_count = 0u;
    usb_cdc_tx_head = 0u;
    usb_cdc_tx_tail = 0u;
    usb_cdc_tx_count = 0u;
    usb_cdc_disconnects = 0u;
    usb_cdc_handled_disconnects = 0u;
    usb_cdc_dtr = false;

    board_init();
    tusb_init();
}

void usb_cdc_poll(void) {
    tud_task();
    if (usb_cdc_session_io_allowed()) {
        usb_cdc_drain_rx();
        usb_cdc_poll_tx();
    } else {
        usb_cdc_reset_session();
    }
}

void usb_cdc_reset_session(void) {
    uint8_t discard[USB_CDC_PACKET_SIZE];

    while (tud_cdc_n_available(USB_CDC_ITF) != 0u) {
        uint32_t count = tud_cdc_n_read(USB_CDC_ITF, discard, sizeof(discard));

        if (count == 0u) {
            break;
        }
    }

    usb_cdc_rx_head = 0u;
    usb_cdc_rx_tail = 0u;
    usb_cdc_rx_count = 0u;
    usb_cdc_tx_head = 0u;
    usb_cdc_tx_tail = 0u;
    usb_cdc_tx_count = 0u;
    (void)tud_cdc_n_write_clear(USB_CDC_ITF);
}

bool usb_cdc_is_connected(void) {
    return tud_ready() && tud_cdc_n_connected(USB_CDC_ITF) && usb_cdc_dtr;
}

uint32_t usb_cdc_disconnect_count(void) {
    return usb_cdc_disconnects;
}

void usb_cdc_acknowledge_session_reset(void) {
    usb_cdc_handled_disconnects = usb_cdc_disconnects;
}

uint32_t usb_cdc_read(void *context, uint8_t *data, uint32_t capacity) {
    uint32_t count = 0u;

    (void)context;

    if ((data == NULL) || (capacity == 0u) || !usb_cdc_session_io_allowed()) {
        return 0u;
    }

    while ((count < capacity) && (usb_cdc_rx_count != 0u)) {
        data[count] = usb_cdc_rx_queue[usb_cdc_rx_tail];
        usb_cdc_rx_tail = (usb_cdc_rx_tail + 1u) % USB_CDC_RX_QUEUE_SIZE;
        usb_cdc_rx_count -= 1u;
        count += 1u;
    }

    return count;
}

bool usb_cdc_write(void *context, const uint8_t *data, uint32_t length) {
    (void)context;

    if ((data == NULL) || (length == 0u) || !usb_cdc_session_io_allowed()) {
        return false;
    }

    if (!usb_cdc_tx_queue_push(data, length)) {
        return false;
    }

    usb_cdc_poll_tx();
    return true;
}

/**
 * @brief TinyUSB callback used to drain received CDC bytes into the local queue.
 * @param itf TinyUSB CDC interface index that received data.
 */
void tud_cdc_rx_cb(uint8_t itf) {
    (void)itf;
    if (usb_cdc_session_io_allowed()) {
        usb_cdc_drain_rx();
    }
}

/**
 * @brief TinyUSB callback for CDC line-state changes.
 * @param itf TinyUSB CDC interface index.
 * @param dtr Host DTR state.
 * @param rts Host RTS state.
 */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf;
    (void)rts;

    if (usb_cdc_dtr && !dtr) {
        usb_cdc_disconnects += 1u;
    }
    usb_cdc_dtr = dtr;
    if (!dtr) {
        usb_cdc_reset_session();
    }
}