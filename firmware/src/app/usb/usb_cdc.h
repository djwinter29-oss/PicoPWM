/**
 * @file usb_cdc.h
 * @brief TinyUSB CDC transport helpers for the PicoPWM CLI.
 */

#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Initialize the TinyUSB device stack and local CDC buffers. */
void usb_cdc_init(void);

/** @brief Run TinyUSB background work and flush queued CDC TX bytes. */
void usb_cdc_poll(void);

/** @brief Return whether the CDC interface is enumerated and the host is connected. */
bool usb_cdc_is_connected(void);

/**
 * @brief Read queued CDC bytes from the local receive queue.
 * @param context Unused transport context.
 * @param data Caller-owned destination buffer.
 * @param capacity Maximum bytes to copy.
 * @return Number of bytes copied into @p data.
 */
uint32_t usb_cdc_read(void *context, uint8_t *data, uint32_t capacity);

/**
 * @brief Queue CDC transmit bytes and attempt to flush them immediately.
 * @param context Unused transport context.
 * @param data Caller-owned payload bytes.
 * @param length Number of bytes to queue.
 * @return `true` when the payload was queued, otherwise `false`.
 */
bool usb_cdc_write(void *context, const uint8_t *data, uint32_t length);

#endif