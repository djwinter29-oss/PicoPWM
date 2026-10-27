/**
 * @file main.c
 * @brief Core 0 startup and top-level service loop for PicoPWM.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "app/cli/pwm_commands.h"
#include "app/i2c/i2c_control_map.h"
#include "platform/led.h"
#include "platform/system.h"
#include "platform/boot_config.h"
#include "device.h"
#include "driver/i2c/i2c_slave.h"
#include "driver/pwm/pwm_driver.h"
#include "app/usb/usb_cdc.h"
#include "app/usb/usb_hid.h"

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

    // Load boot settings first: the clock must be set before any PWM backend caches it for timing calculations.
    boot_config_load();
    (void)system_init_clock();

    led_init();

    // USB CDC command interface.
    usb_cdc_init();
    pwm_commands_init(&pwm_command_session, &shell_transport);

    // Configure and launch all three PWM banks before starting Core 1.
    const pwm_role_t bank_roles[PWM_BANK_COUNT] = {boot_config_get_bank(PWM_BANK_A), boot_config_get_bank(PWM_BANK_B),
                                                   boot_config_get_bank(PWM_BANK_C)};
    if (!pwm_driver_init(bank_roles)) {
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
    i2c_slave_init(boot_config_get_i2c_address(), &i2c_control_map_ops);

    // Core 0 main loop: service USB CDC, USB HID, and I2C.
    while (true) {
        bool usb_connected;

        system_watchdog_kick();
        usb_cdc_poll();
        usb_hid_poll();
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
