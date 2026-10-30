/**
 * @file usb_hid.c
 * @brief TinyUSB glue for the PicoPWM HID vendor interface; report logic lives in hid_protocol.c.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#include "app/usb/usb_hid.h"

#include "app/usb/hid_protocol.h"

#include "tusb.h"

void usb_hid_poll(void) {
    hid_protocol_poll();
}

/** @brief Clear the report 20 result when the host (re)connects or disconnects. */
void tud_mount_cb(void) {
    hid_protocol_reset();
}

/** @copydoc tud_mount_cb */
void tud_umount_cb(void) {
    hid_protocol_reset();
}

/**
 * @brief TinyUSB callback filling a Feature GET_REPORT payload.
 * @return Payload bytes written, or 0 for any report that is not a defined Feature report.
 */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t *buffer,
                               uint16_t reqlen) {
    (void)instance;
    if (type != HID_REPORT_TYPE_FEATURE) {
        return 0u;
    }
    return hid_protocol_get(report_id, buffer, reqlen);
}

/** @brief TinyUSB callback copying a Feature SET_REPORT for report 20 into the request slot. */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t const *buffer,
                           uint16_t bufsize) {
    (void)instance;
    if (type == HID_REPORT_TYPE_FEATURE && report_id == HID_REPORT_ID_ACTION) {
        (void)hid_protocol_submit(buffer, bufsize);
    }
}
