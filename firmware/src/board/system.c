/**
 * @file system.c
 * @brief Board-level clock and reboot helpers for PicoPWM.
 */

#include "board/system.h"

#include "hardware/clocks.h"
#include "hardware/watchdog.h"

/** @copydoc system_init_clock */
void system_init_clock(void) {
    if (!set_sys_clock_khz(SYSTEM_CLOCK, true)) {
        /* Keep the SDK-selected clock when the requested overclock is unavailable. */
    }
}

/** @copydoc system_reboot */
void system_reboot(void) {
    watchdog_reboot(0u, 0u, 0u);
}