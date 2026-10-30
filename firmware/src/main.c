/**
 * @file main.c
 * @brief Core 0 startup and top-level service loop for PicoPWM.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "app/cli/cli_app.h"
#include "app/i2c/i2c_control_map.h"
#include "platform/led.h"
#include "platform/system.h"
#include "platform/boot_config.h"
#include "control_api.h"
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
    static cli_app_t cli_app;

    // Load boot settings first: the clock must be set before any PWM backend caches it for timing calculations.
    boot_config_load();
    (void)system_init_clock();

    led_init();

    // USB CDC command interface.
    usb_cdc_init();
    pwm_driver_set_wait_hook(core0_service_while_pwm_applies);
    (void)cli_app_init(&cli_app);

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
    i2c_control_map_init();
    i2c_slave_init(boot_config_get_i2c_address(), &i2c_control_map_ops);

    // Core 0 main loop: service USB CDC, USB HID, and I2C.
    while (true) {
        absolute_time_t service_start = get_absolute_time();

        system_watchdog_kick();
        usb_cdc_poll();
        cli_app_poll(&cli_app);
        usb_hid_poll();
        i2c_control_map_poll();
        system_record_service((uint32_t)absolute_time_diff_us(service_start, get_absolute_time()));
        sleep_us(100);
    }

    return 0;
}
