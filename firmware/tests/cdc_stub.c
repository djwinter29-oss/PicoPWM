/**
 * @file cdc_stub.c
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#include "tusb.h"

#include <string.h>

static bool ready = true;
static bool connected = true;
static uint8_t rx[512];
static uint32_t rx_length;
static uint32_t rx_offset;
static uint32_t tx_space = 64u;
static uint32_t tx_accepted;
static uint32_t tx_buffered;
static uint32_t tx_clear_count;

void board_init(void) {}
void tusb_init(void) {}
void tud_task(void) {}

void cdc_test_set_ready(bool connected) {
    ready = connected;
}
void cdc_test_set_connected(bool is_connected) {
    connected = is_connected;
}
void cdc_test_load_rx(const uint8_t *data, uint32_t length) {
    memcpy(rx, data, length);
    rx_length = length;
    rx_offset = 0u;
}
uint32_t cdc_test_rx_remaining(void) {
    return rx_length - rx_offset;
}
void cdc_test_set_tx_space(uint32_t space) {
    tx_space = space;
}
uint32_t cdc_test_tx_clear_count(void) {
    return tx_clear_count;
}
uint32_t cdc_test_tx_buffered(void) {
    return tx_buffered;
}

bool tud_ready(void) {
    return ready;
}
bool tud_cdc_n_connected(uint8_t itf) {
    (void)itf;
    return ready && connected;
}
uint32_t tud_cdc_n_available(uint8_t itf) {
    (void)itf;
    return rx_length - rx_offset;
}
uint32_t tud_cdc_n_read(uint8_t itf, void *buffer, uint32_t length) {
    uint32_t left = rx_length - rx_offset;

    (void)itf;
    if (length > left) {
        length = left;
    }
    memcpy(buffer, &rx[rx_offset], length);
    rx_offset += length;
    return length;
}
uint32_t tud_cdc_n_write_available(uint8_t itf) {
    (void)itf;
    return tx_space;
}
uint32_t tud_cdc_n_write(uint8_t itf, const void *buffer, uint32_t length) {
    (void)itf;
    (void)buffer;
    if (length > tx_space) {
        length = tx_space;
    }
    tx_space -= length;
    tx_accepted += length;
    tx_buffered += length;
    return length;
}
void tud_cdc_n_write_flush(uint8_t itf) {
    (void)itf;
}
bool tud_cdc_n_write_clear(uint8_t itf) {
    (void)itf;
    ++tx_clear_count;
    tx_buffered = 0u;
    return true;
}
