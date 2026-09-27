/**
 * @file main.c
 * @brief Core 0 startup and top-level service loop for PicoPWM.
 */

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include "cli/pwm_commands.h"
#include "board/led.h"
#include "i2c/i2c_slave.h"
#include "pwmdriver/pwm_driver.h"
#include "usb/usb_cdc.h"

/**
 * @brief Initialize Core 0 services, launch Core 1 PWM ownership, and run the main loop.
 * @return Never returns during normal firmware operation.
 */
int main(void) {
    static pwm_commands_t pwm_command_session;
    static const shell_transport_t shell_transport = {
        .read = usb_cdc_read,
        .write = usb_cdc_write,
        .context = NULL,
    };
    bool usb_was_connected = false;

    // Overclock to 150 MHz for more CPU headroom and higher HW PWM max frequency.
    // Falls back to the existing clock if this fails (e.g., silicon limits).
    if (!set_sys_clock_khz(150000, true)) {
        // If 150 MHz fails, it usually remains at the previous frequency (125 MHz default).
        // We continue anyway because the firmware works at both speeds.
    }

    led_init();

    // USB CDC command interface.
    usb_cdc_init();
    pwm_commands_init(&pwm_command_session, &shell_transport);

    // Launch Core 1 to manage all PWM hardware.
    pwm_driver_launch();

    // Wait for Core 1 mailbox service before accepting commands.
    while (!pwm_driver_is_ready()) {
        tight_loop_contents();
    }

    // Core 0: start communication interfaces.
    i2c_slave_init();

    // Core 0 main loop: service USB CDC and I2C.
    while (true) {
        bool usb_connected;

        usb_cdc_poll();
        // Detect USB connection events and print the initial CLI help.
        usb_connected = usb_cdc_is_connected();
        if (usb_connected && !usb_was_connected) {
            pwm_commands_on_connected(&pwm_command_session);
        }
        usb_was_connected = usb_connected;
        pwm_commands_poll(&pwm_command_session);
        i2c_slave_poll();
        sleep_us(100);
    }

    return 0;
}
