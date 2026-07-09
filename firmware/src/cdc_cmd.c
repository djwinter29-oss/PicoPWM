#include "cdc_cmd.h"
#include "cmd_parser.h"
#include "pico/stdlib.h"

void cdc_cmd_init(void) {
    // stdio_usb is enabled via CMake; stdio_init_all() is called from main.
    // This module reads from the stdio USB RX path in a non-blocking loop.
    cmd_parser_init();
}

void cdc_cmd_poll(void) {
    int ch;
    while ((ch = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        cmd_parser_process_char((char)ch);
    }
}
