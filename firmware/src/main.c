/**
 * @file main.c
 * @brief Core 0 startup and top-level service loop for PicoPWM.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "cli/pwm_commands.h"
#include "board/led.h"
#include "board/system.h"
#include "device_api/device_api.h"
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

    // Overclock before any PWM backend caches the system clock for timing calculations.
    (void)system_init_clock();

    led_init();

    // USB CDC command interface.
    usb_cdc_init();
    pwm_commands_init(&pwm_command_session, &shell_transport);

    // Configure and launch all three PWM banks before starting Core 1.
    pwm_driver_config_t pwm_config;
    pwm_driver_config_load_target(&pwm_config);
    if (!device_api_config_init(&pwm_config, &pwm_config) || !pwm_driver_init(&pwm_config)) {
        system_reboot();
    }

    system_watchdog_start();

    // Wait for Core 1 mailbox service before accepting commands.
    absolute_time_t ready_deadline = make_timeout_time_ms(2000);
    while (!pwm_driver_is_ready()) {
        system_watchdog_kick();
        if (pwm_driver_startup_failed() || time_reached(ready_deadline)) {
            system_reboot();
        }
        tight_loop_contents();
    }

    // Core 0: start communication interfaces.
    i2c_slave_init(pwm_config.i2c_address);
    pwm_driver_set_wait_hook(i2c_slave_service_reads);

    // Core 0 main loop: service USB CDC and I2C.
    while (true) {
        bool usb_connected;

        system_watchdog_kick();
        i2c_slave_service_reads();
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
