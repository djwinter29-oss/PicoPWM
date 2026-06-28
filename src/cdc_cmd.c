#include "cdc_cmd.h"
#include "cmd_parser.h"
#include "pico/stdlib.h"
#include "tusb.h"

void cdc_cmd_init(void) {
    // stdio_usb is enabled via CMake; stdio_init_all() is called from main.
    // This module uses the TinyUSB CDC functions directly for non-blocking reads.
    cmd_parser_init();
}

void cdc_cmd_poll(void) {
    // Service TinyUSB stack. Required when using tud_cdc_* directly.
    tud_task();

    while (tud_cdc_available()) {
        char c = tud_cdc_read_char();
        cmd_parser_process_char(c);
    }
}
