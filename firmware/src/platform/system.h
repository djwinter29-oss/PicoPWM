/**
 * @file system.h
 * @brief Board-level clock and reboot helpers used by the PicoPWM firmware.
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Board-family string exposed to host-side status and test code. */
#if defined(PICO_RP2350A) || defined(PICO_RP2350B)
#define SYSTEM_BOARD_FAMILY "pico2"
#else
#define SYSTEM_BOARD_FAMILY "pico"
#endif

/** @brief Nominal system clock target in kHz for the current board family. */
#if defined(PICO_RP2350A) || defined(PICO_RP2350B)
#define SYSTEM_CLOCK 150000u
#else
#define SYSTEM_CLOCK 150000u
#endif

/** @brief Configure the board system clock for the PicoPWM firmware runtime. */
bool system_init_clock(void);

/** @brief Return true when the nominal system-clock target was applied. */
bool system_clock_is_at_target(void);

/** @brief Return the system clock frequency the backends should use, in Hz. */
uint32_t system_clock_hz(void);

/** @brief Start the runtime watchdog used to recover a stuck Core 0 loop. */
void system_watchdog_start(void);

/** @brief Pet the runtime watchdog from Core 0 service paths. */
void system_watchdog_kick(void);

/** @brief Reboot the board through the watchdog reset path. */
void system_reboot(void);

#endif