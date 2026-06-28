#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "control.h"
#include "cmd_parser.h"
#include "cdc_cmd.h"
#include "i2c_slave.h"

int main(void) {
    // Overclock to 150 MHz for more CPU headroom and higher HW PWM max frequency.
    // Falls back to the existing clock if this fails (e.g., silicon limits).
    if (!set_sys_clock_khz(150000, true)) {
        // If 150 MHz fails, it usually remains at the previous frequency (125 MHz default).
        // We continue anyway because the firmware works at both speeds.
    }

    // USB CDC stdio/command interface is initialized here.
    stdio_init_all();

    printf("\r\n");
    printf("========================================\r\n");
    printf("  PicoPWM starting at %lu MHz\r\n", clock_get_hz(clk_sys) / 1000000);
    printf("  24 control channels: 0..7=HW, 8..23=SW\r\n");
    printf("  Each channel: freq, duty, pulse_count\r\n");
    printf("  USB CDC serial command interface\r\n");
    printf("  I2C slave on GPIO 16/17, addr 0x40\r\n");
    printf("========================================\r\n\n");

    hw_pwm_init();
    sw_pwm_init();
    control_init();
    cdc_cmd_init();
    i2c_slave_init();

    // Optional demo pattern.
    control_set_freq(0, 1000.0f);     // HW0 1 kHz, 50% duty
    control_set_duty(0, 0.50f);
    control_set_freq(1, 10000.0f);    // HW1 10 kHz, 25% duty
    control_set_duty(1, 0.25f);
    control_set_freq(2, 100000.0f);   // HW2 100 kHz, 10% duty
    control_set_duty(2, 0.10f);

    control_set_freq(8, 100.0f);      // SW0 100 Hz, 50% duty
    control_set_duty(8, 0.50f);
    control_set_freq(9, 10.0f);       // SW1 10 Hz, 25% duty
    control_set_duty(9, 0.25f);
    control_set_freq(10, 1.0f);       // SW2 1 Hz, 10% duty
    control_set_duty(10, 0.10f);

    print_help();

    while (true) {
        cdc_cmd_poll();
        i2c_slave_poll();
        sleep_us(100);
    }

    return 0;
}
