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
 * @brief Keep the USB device stack moving while Core 0 waits for a PWM apply.
 * @note Shell parsing, HID action execution, and I2C write execution stay in the main loop.
 *       Running them here would submit another mailbox command under the control lock.
 */
static void core0_service_while_pwm_applies(void) {
    usb_cdc_poll();
}

/**
 * @brief Initialize Core 0 services, launch Core 1 PWM ownership, and run the main loop.
 * @return Never returns during normal firmware operation.
 */
int main(void) {
    static pwm_commands_t pwm_command_session;
    static const char cli_init_error[] = "ERR CDC CLI initialization failed\r\n";
    static const shell_transport_t shell_transport = {
        .read = usb_cdc_read,
        .write = usb_cdc_write,
        .context = NULL,
    };
    bool cli_initialized;
    bool cli_connection_output_complete = false;
    bool cli_init_error_pending;
    bool usb_was_connected = false;

    // Load boot settings first: the clock must be set before any PWM backend caches it for timing calculations.
    boot_config_load();
    (void)system_init_clock();

    led_init();

    // USB CDC command interface.
    usb_cdc_init();
    pwm_driver_set_wait_hook(core0_service_while_pwm_applies);
    cli_initialized = pwm_commands_init(&pwm_command_session, &shell_transport);
    cli_init_error_pending = !cli_initialized;

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
        absolute_time_t service_start = get_absolute_time();

        system_watchdog_kick();
        usb_cdc_poll();
        usb_hid_poll();
        // Retry connection output until the bounded CDC queue accepts it.
        usb_connected = usb_cdc_is_connected();
        if (usb_connected && cli_initialized && !cli_connection_output_complete) {
            cli_connection_output_complete = pwm_commands_on_connected(&pwm_command_session);
        } else if (usb_connected && cli_init_error_pending &&
                   usb_cdc_write(NULL, (const uint8_t *)cli_init_error, sizeof(cli_init_error) - 1u)) {
            cli_init_error_pending = false;
        } else if (!usb_connected && usb_was_connected) {
            pwm_commands_on_disconnected(&pwm_command_session);
            cli_connection_output_complete = false;
            cli_init_error_pending = !cli_initialized;
        }
        usb_was_connected = usb_connected;
        if (cli_initialized) {
            pwm_commands_poll(&pwm_command_session);
        }
        i2c_slave_poll();
        system_record_service((uint32_t)absolute_time_diff_us(service_start, get_absolute_time()));
        sleep_us(100);
    }

    return 0;
}
