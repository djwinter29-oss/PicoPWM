/**
 * @file hid_protocol.h
 * @brief PicoPWM HID feature-report contract; see docs/design/usb/hid-report-reference.md.
 *
 * This layer owns report layouts and the report 20 transaction. It has no TinyUSB dependency and reaches the
 * PWM runtime only through `control_api`.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#ifndef HID_PROTOCOL_H
#define HID_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Feature report IDs. Each usage ID in the descriptor equals its report ID. */
typedef enum {
    HID_REPORT_ID_IDENTITY = 1,      /**< Device identity and capabilities, host reads. */
    HID_REPORT_ID_CONFIGURATION = 2, /**< Running and target configuration, host reads. */
    HID_REPORT_ID_MEMORY = 3,        /**< Static SRAM reservation and physical capacity, host reads. */
    HID_REPORT_ID_RUNTIME = 4,       /**< Uptime, temperature, and service load, host reads. */
    HID_REPORT_ID_PINS = 5,          /**< Board GPIO allocation, host reads. */
    HID_REPORT_ID_BANK_STATUS = 10,  /**< First of three bank status reports (10-12), host reads. */
    HID_REPORT_ID_ACTION = 20,       /**< Action request (SET) and latest result (GET). */
} hid_report_id_t;

/** @brief Payload sizes, excluding the report ID byte. */
enum {
    HID_IDENTITY_SIZE = 18,                      /**< Report 1 payload bytes. */
    HID_CONFIGURATION_SIZE = 21,                 /**< Report 2 payload bytes. */
    HID_MEMORY_SIZE = 9,                         /**< Report 3 version and two uint32 byte counts. */
    HID_RUNTIME_SIZE = 13,                       /**< Report 4 runtime telemetry payload bytes. */
    HID_PINS_SIZE = 28,                          /**< Report 5 version, 24 channel GPIOs, SDA, SCL, LED. */
    HID_BANK_STATUS_SIZE = 41,                   /**< Reports 10-12 payload bytes. */
    HID_ACTION_SIZE = 12,                        /**< Report 20 payload bytes, in both directions. */
    HID_MAX_PAYLOAD_SIZE = HID_BANK_STATUS_SIZE, /**< Largest payload of any report. */
};

/** @brief Report 20 operations. */
typedef enum {
    HID_OP_SET_CHANNEL = 1,       /**< Set one generator channel. */
    HID_OP_STOP = 2,              /**< Restore defaults and stop outputs. */
    HID_OP_SET_BANK = 3,          /**< Set a target bank backend and role. */
    HID_OP_SET_I2C_ADDRESS = 4,   /**< Set the target I2C address. */
    HID_OP_SAVE_CONFIG = 5,       /**< Persist the target configuration. */
    HID_OP_SET_CLOCK_PROFILE = 6, /**< Set the target clock profile. */
    HID_OP_SAVE_REBOOT = 7,       /**< Save the target and reboot after the host reads success. */
    HID_OP_SET_LED = 8,           /**< Set the identification LED from arg0 (0 or 1). */
    HID_OP_REBOOT = 9,            /**< Reboot without saving after confirmed result readback. */
} hid_operation_t;

/** @brief Report 20 result status. Values 0-5 equal `pwm_driver_result_t`. */
typedef enum {
    HID_STATUS_OK = 0,             /**< Completed. */
    HID_STATUS_BUSY = 1,           /**< PWM control plane was busy. */
    HID_STATUS_INVALID = 2,        /**< Request failed validation; nothing changed. */
    HID_STATUS_UNAVAILABLE = 3,    /**< Core 1 could not take the command. */
    HID_STATUS_TIMEOUT = 4,        /**< Core 1 did not claim the command in time. */
    HID_STATUS_APPLY_FAILED = 5,   /**< A backend rejected the request. */
    HID_STATUS_PENDING = 6,        /**< Accepted, not yet executed. */
    HID_STATUS_UNSUPPORTED = 7,    /**< Unknown layout version or operation. */
    HID_STATUS_STORAGE_FAILED = 8, /**< Configuration write or verification failed. */
} hid_status_t;

/**
 * @brief Fill one GET_REPORT payload.
 * @param report_id Feature report ID.
 * @param payload Destination for the payload, without the report ID byte.
 * @param capacity Bytes available in @p payload.
 * @return Payload length, or 0 for an unknown ID, a short buffer, or unreadable state.
 */
uint16_t hid_protocol_get(uint8_t report_id, uint8_t *payload, uint16_t capacity);

/**
 * @brief Accept a report 20 SET payload into the single request slot.
 * @param payload Request payload, without the report ID byte.
 * @param length Payload length; anything but 12 is dropped.
 * @return `true` when accepted. A drop leaves the previous result untouched.
 * @note Callable from the TinyUSB callback; execution happens later in hid_protocol_poll().
 */
bool hid_protocol_submit(const uint8_t *payload, uint16_t length);

/** @brief Validate and execute the pending request, then publish its final result and free the slot. */
void hid_protocol_poll(void);

/** @brief Clear the result to its initial state and free the slot; called on USB reset and disconnect. */
void hid_protocol_reset(void);

#endif
