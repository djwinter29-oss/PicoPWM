/**
 * @file system.c
 * @brief Board-level clock and reboot helpers for PicoPWM.
 */

#include "board/system.h"

#include "hardware/clocks.h"
#include "hardware/watchdog.h"

/** @brief Runtime watchdog period in milliseconds. Longer than one flash save. */
#define SYSTEM_WATCHDOG_TIMEOUT_MS 5000u

/** @brief True when the requested system-clock target was accepted. */
static bool system_clock_at_target;

/** @copydoc system_init_clock */
bool system_init_clock(void) {
    /*
     * PWM backends sample clock_get_hz(clk_sys) during their own init, which runs
     * after this call. A rejected target therefore changes the realized frequency
     * base, and divider math still matches the clock that is actually running.
     */
    system_clock_at_target = set_sys_clock_khz(SYSTEM_CLOCK, true);
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
    watchdog_enable(SYSTEM_WATCHDOG_TIMEOUT_MS, true);
}

/** @copydoc system_watchdog_kick */
void system_watchdog_kick(void) {
    watchdog_update();
}

/** @copydoc system_reboot */
void system_reboot(void) {
    watchdog_reboot(0u, 0u, 0u);
}