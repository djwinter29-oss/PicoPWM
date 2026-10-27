/**
 * @file system.c
 * @brief Board-level clock and reboot helpers for PicoPWM.
 */

#include "platform/system.h"

#include "platform/boot_config.h"

#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "hardware/adc.h"
#include "pico/time.h"

/** @brief Runtime watchdog period in milliseconds. Longer than one flash save. */
#define SYSTEM_WATCHDOG_TIMEOUT_MS 5000u

/** @brief True when the requested system-clock profile was accepted, or none was requested. */
static bool system_clock_at_target;
/** @brief True once a transport has scheduled a reboot. */
static bool reboot_pending;
/** @brief Core 0 reboot deadline, checked when servicing the watchdog. */
static absolute_time_t reboot_deadline;
/** @brief Core 0 service measurement window start. */
static absolute_time_t service_window;
/** @brief Service work accumulated in the current window, excluding idle sleep. */
static uint64_t service_busy_us;
/** @brief Last completed Core 0 service load in basis points. */
static uint16_t service_load;

/** @copydoc system_init_clock */
bool system_init_clock(void) {
    const uint16_t mhz = boot_config_get_sysclk_mhz();

    /*
     * PWM backends sample clock_get_hz(clk_sys) during their own init, which runs
     * after this call. A rejected target therefore changes the realized frequency
     * base, and divider math still matches the clock that is actually running.
     */
    system_clock_at_target = mhz == 0u || set_sys_clock_khz((uint32_t)mhz * 1000u, true);
    return system_clock_at_target;
}

/** @copydoc system_clock_is_at_target */
bool system_clock_is_at_target(void) {
    return system_clock_at_target;
}

/** @copydoc system_clock_hz */
uint32_t system_clock_hz(void) {
    return clock_get_hz(clk_sys);
}

/** @copydoc system_watchdog_start */
void system_watchdog_start(void) {
    service_window = get_absolute_time();
    adc_init();
    adc_set_temp_sensor_enabled(true);
    watchdog_enable(SYSTEM_WATCHDOG_TIMEOUT_MS, true);
}

void system_record_service(uint32_t elapsed_us) {
    service_busy_us += elapsed_us;
    absolute_time_t now = get_absolute_time();
    uint64_t window_us = (uint64_t)absolute_time_diff_us(service_window, now);
    if (window_us >= 1000000u) {
        uint64_t load = service_busy_us * 10000u / window_us;
        service_load = (uint16_t)(load > 10000u ? 10000u : load);
        service_busy_us = 0u;
        service_window = now;
    }
}

uint16_t system_service_load(void) {
    return service_load;
}

int32_t system_temperature_millic(void) {
    adc_select_input(4u);
    uint16_t raw = adc_read();
    if (raw == 0u || raw == 4095u) {
        return INT32_MIN;
    }
    float voltage = (float)raw * (3.3f / 4096.0f);
    return (int32_t)((27.0f - (voltage - 0.706f) / 0.001721f) * 1000.0f);
}

/** @copydoc system_watchdog_kick */
void system_watchdog_kick(void) {
    if (reboot_pending && time_reached(reboot_deadline)) {
        system_reboot();
    }
    watchdog_update();
}

void system_schedule_reboot(uint32_t delay_ms) {
    reboot_deadline = make_timeout_time_ms(delay_ms);
    reboot_pending = true;
}

/** @copydoc system_reboot */
void system_reboot(void) {
    watchdog_reboot(0u, 0u, 0u);
}