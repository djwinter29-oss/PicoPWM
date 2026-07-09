#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "cli/cli_shell.h"
#include "cli/device_cli.h"
#include "i2c/i2c_slave.h"
#include "pwmdriver/pwm_driver.h"
#include "usb/usb_cdc.h"

static uint32_t main_usb_cli_read(void *context, uint8_t *data, uint32_t capacity) {
    (void)context;
    return usb_cdc_read(data, capacity);
}

static bool main_usb_cli_write(void *context, const uint8_t *data, uint32_t length) {
    (void)context;
    return usb_cdc_write(data, length);
}

int main(void) {
    static const cli_shell_transport_t usb_cli_transport = {
        .read = main_usb_cli_read,
        .write = main_usb_cli_write,
        .context = NULL,
    };

    // Overclock to 150 MHz for more CPU headroom and higher HW PWM max frequency.
    // Falls back to the existing clock if this fails (e.g., silicon limits).
    if (!set_sys_clock_khz(150000, true)) {
        // If 150 MHz fails, it usually remains at the previous frequency (125 MHz default).
        // We continue anyway because the firmware works at both speeds.
    }

    // USB CDC command interface.
    usb_cdc_init();
    device_cli_init(&usb_cli_transport);

    // Launch Core 1 to manage all PWM hardware.
    pwm_driver_launch();

    // Wait for Core 1 to finish PWM init before accepting commands.
    while (!pwm_driver_is_ready()) {
        tight_loop_contents();
    }

    // Core 0: start communication interfaces.
    i2c_slave_init();

    while (!usb_cdc_is_connected()) {
        usb_cdc_poll();
        tight_loop_contents();
    }

    cli_shell_write_line("========================================");
    {
        char line[64];
        snprintf(line, sizeof(line), "  PicoPWM starting at %lu MHz", clock_get_hz(clk_sys) / 1000000);
        cli_shell_write_line(line);
    }
    cli_shell_write_line("  Dual-core: Core0=CDC+I2C, Core1=PWM");
    cli_shell_write_line("  24 channels: 0..7=HW, 8..15=PIO, 16..23=SW");
    cli_shell_write_line("  Init: freq=0, duty=50%, pulses=0");
    cli_shell_write_line("  USB CDC serial + I2C slave (addr 0x40)");
    cli_shell_write_line("========================================");
    cli_shell_write_line(NULL);
    device_cli_print_help();

    // Core 0 main loop: service USB CDC and I2C.
    while (true) {
        usb_cdc_poll();
        device_cli_poll();
        i2c_slave_poll();
        sleep_us(100);
    }

    return 0;
}
