// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong

/**
 * @file usb_hid.h
 * @brief TinyUSB HID vendor interface for PicoPWM configuration and status reports.
 */

#ifndef USB_HID_H
#define USB_HID_H

/** @brief Number of HID interfaces exposed by the firmware. */
#define USB_HID_COUNT 1u

/** @brief Execute the pending HID action, if any; call from the Core 0 main loop after TinyUSB runs. */
void usb_hid_poll(void);

#endif
