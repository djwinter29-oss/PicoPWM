#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "pico/multicore.h"
#include <stdio.h>
#include "control.h"
#include "cmd_parser.h"
#include "cdc_cmd.h"
#include "i2c_slave.h"
#include "pwmdriver/pwm_driver.h"

int main(void) {
    // Overclock to 150 MHz for more CPU headroom and higher HW PWM max frequency.
    // Falls back to the existing clock if this fails (e.g., silicon limits).
    if (!set_sys_clock_khz(150000, true)) {
        // If 150 MHz fails, it usually remains at the previous frequency (125 MHz default).
        // We continue anyway because the firmware works at both speeds.
    }

    // USB CDC stdio / command interface.
    stdio_init_all();

    printf("\r\n");
    printf("========================================\r\n");
    printf("  PicoPWM starting at %lu MHz\r\n", clock_get_hz(clk_sys) / 1000000);
    printf("  Dual-core: Core0=CDC+I2C, Core1=PWM\r\n");
    printf("  24 channels: 0..7=HW, 8..15=PIO, 16..23=SW\r\n");
    printf("  Init: freq=0, duty=50%%, pulses=0\r\n");
    printf("  USB CDC serial + I2C slave (addr 0x40)\r\n");
    printf("========================================\r\n\n");

    // Core 0: initialise cached values + command queue.
    control_init();

    // Launch Core 1 to manage all PWM hardware.
    pwm_driver_launch();

    // Wait for Core 1 to finish PWM init before accepting commands.
    while (!pwm_driver_is_ready()) {
        tight_loop_contents();
    }

    // Core 0: start communication interfaces.
    cdc_cmd_init();
    i2c_slave_init();

    print_help();

    // Core 0 main loop: service USB CDC and I2C.
    while (true) {
        cdc_cmd_poll();
        i2c_slave_poll();
        sleep_us(100);
    }

    return 0;
}
