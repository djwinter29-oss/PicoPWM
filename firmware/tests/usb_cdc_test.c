#include "app/usb/usb_cdc.h"
#include "app/cli/cli_shell.h"
#include "tusb.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void tud_cdc_rx_cb(uint8_t itf);
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts);

typedef struct {
    uint32_t blocking_calls;
    uint32_t followup_calls;
} disconnect_test_context_t;

static bool test_blocking_command(void *context, int argc, const char *const *argv) {
    disconnect_test_context_t *test = context;
    static const uint8_t followup[] = "ping\r";
    uint8_t byte;

    assert(argc == 1);
    assert(strcmp(argv[0], "block") == 0);
    ++test->blocking_calls;

    cdc_test_set_connected(true);
    tud_cdc_line_state_cb(0u, false, false);
    cdc_test_load_rx(followup, sizeof(followup) - 1u);
    usb_cdc_poll();
    assert(usb_cdc_read(NULL, &byte, sizeof(byte)) == 0u);
    assert(cdc_test_rx_remaining() == 0u);
    return true;
}

static bool test_followup_command(void *context, int argc, const char *const *argv) {
    disconnect_test_context_t *test = context;

    assert(argc == 1);
    assert(strcmp(argv[0], "ping") == 0);
    ++test->followup_calls;
    return true;
}

static void test_disconnect_during_command_discards_buffered_input(void) {
    static const shell_command_t commands[] = {
        {"block", "block", test_blocking_command},
        {"ping", "ping", test_followup_command},
    };
    static const char input[] = "block\rping\r";
    shell_t shell;
    disconnect_test_context_t context = {0};
    shell_transport_t transport = {
        .read = usb_cdc_read,
        .write = usb_cdc_write,
        .context = NULL,
    };
    shell_config_t config = {
        .transport = &transport,
        .commands = commands,
        .command_count = sizeof(commands) / sizeof(commands[0]),
        .unknown_message = "unknown",
        .unknown_handler = NULL,
        .command_context = &context,
    };

    usb_cdc_init();
    usb_cdc_reset_session();
    cdc_test_set_ready(true);
    cdc_test_set_connected(true);
    cdc_test_set_tx_space(64u);
    tud_cdc_line_state_cb(0u, true, true);
    assert(shell_init(&shell, &config));
    cdc_test_load_rx((const uint8_t *)input, sizeof(input) - 1u);
    usb_cdc_poll();
    assert(cdc_test_rx_remaining() == 0u);

    shell_poll(&shell);
    assert(context.blocking_calls == 1u);
    assert(context.followup_calls == 0u);

    usb_cdc_reset_session();
    assert(shell_init(&shell, &config));
    usb_cdc_acknowledge_session_reset();
    cdc_test_set_connected(true);
    tud_cdc_line_state_cb(0u, true, true);
    cdc_test_load_rx((const uint8_t *)"ping\r", sizeof("ping\r") - 1u);
    usb_cdc_poll();
    shell_poll(&shell);
    assert(context.blocking_calls == 1u);
    assert(context.followup_calls == 1u);
}

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
    tud_cdc_line_state_cb(0u, true, true);
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
    usb_cdc_reset_session();
    assert(cdc_test_rx_remaining() == 0u);
    assert(usb_cdc_read(NULL, readback, sizeof(readback)) == 0u);

    cdc_test_set_tx_space(0u);
    cdc_test_set_connected(false);
    uint32_t disconnect_count = usb_cdc_disconnect_count();
    tud_cdc_line_state_cb(0u, false, false);
    assert(usb_cdc_disconnect_count() == disconnect_count + 1u);
    assert(cdc_test_tx_buffered() == 0u);
    cdc_test_load_rx(payload, sizeof(payload));
    usb_cdc_poll();
    assert(cdc_test_rx_remaining() == 0u);
    assert(usb_cdc_read(NULL, readback, sizeof(readback)) == 0u);
    assert(!usb_cdc_write(NULL, bulk, sizeof(bulk)));
    uint32_t clear_count = cdc_test_tx_clear_count();
    usb_cdc_poll();
    assert(cdc_test_tx_clear_count() > clear_count);

    cdc_test_set_connected(true);
    tud_cdc_line_state_cb(0u, true, true);
    assert(!usb_cdc_write(NULL, bulk, sizeof(bulk)));
    usb_cdc_acknowledge_session_reset();
    assert(usb_cdc_write(NULL, bulk, sizeof(bulk)));
    assert(!usb_cdc_write(NULL, payload, 1u));

    test_disconnect_during_command_discards_buffered_input();

    puts("usb_cdc_test ok");
    return 0;
}
