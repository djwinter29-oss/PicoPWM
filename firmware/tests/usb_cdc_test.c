#include "app/usb/usb_cdc.h"
#include "tusb.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void tud_cdc_rx_cb(uint8_t itf);
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts);

int main(void) {
    uint8_t payload[200];
    uint8_t bulk[2048];
    uint8_t readback[128];
    uint8_t chunk[64];

    for (uint32_t i = 0u; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)i;
    }
    memset(bulk, 0xA5, sizeof(bulk));

    usb_cdc_init();
    cdc_test_set_ready(false);
    assert(!usb_cdc_is_connected());
    assert(!usb_cdc_write(NULL, payload, 4u));
    cdc_test_set_ready(true);
    assert(usb_cdc_is_connected());
    assert(!usb_cdc_write(NULL, NULL, 4u));
    assert(!usb_cdc_write(NULL, payload, 0u));
    assert(usb_cdc_write(NULL, payload, 4u));
    cdc_test_set_tx_space(0u);
    assert(usb_cdc_write(NULL, payload, 4u));
    assert(usb_cdc_write(NULL, bulk, 900u));
    assert(usb_cdc_write(NULL, bulk, 120u));
    assert(usb_cdc_write(NULL, bulk, 1024u));
    assert(!usb_cdc_write(NULL, bulk, 1u));
    assert(usb_cdc_read(NULL, NULL, 4u) == 0u);

    cdc_test_load_rx(payload, sizeof(payload));
    usb_cdc_poll();
    tud_cdc_rx_cb(0u);
    assert(cdc_test_rx_remaining() == sizeof(payload) - 128u);
    assert(usb_cdc_read(NULL, readback, sizeof(readback)) == 128u);
    for (uint32_t i = 0u; i < sizeof(readback); ++i) {
        assert(readback[i] == payload[i]);
    }

    usb_cdc_poll();
    assert(cdc_test_rx_remaining() == 0u);
    assert(usb_cdc_read(NULL, chunk, sizeof(chunk)) == 64u);
    assert(chunk[0] == payload[128]);
    assert(usb_cdc_read(NULL, chunk, sizeof(chunk)) == 8u);
    tud_cdc_line_state_cb(0u, true, true);

    cdc_test_load_rx(payload, sizeof(payload));
    usb_cdc_poll();
    assert(cdc_test_rx_remaining() == sizeof(payload) - 128u);
    usb_cdc_reset_rx();
    assert(cdc_test_rx_remaining() == 0u);
    assert(usb_cdc_read(NULL, readback, sizeof(readback)) == 0u);

    puts("usb_cdc_test ok");
    return 0;
}
