/**
 * @file system.h
 * @brief Board-level clock and reboot helpers used by the PicoPWM firmware.
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Apply the saved system-clock profile; with none saved, keep the SDK default (SYS_CLK_KHZ). */
bool system_init_clock(void);

/** @brief Return true when the saved profile was applied, or none was requested. */
bool system_clock_is_at_target(void);

/** @brief Return the system clock frequency the backends should use, in Hz. */
uint32_t system_clock_hz(void);

/** @brief Start the runtime watchdog used to recover a stuck Core 0 loop. */
void system_watchdog_start(void);

/** @brief Pet the runtime watchdog from Core 0 service paths. */
void system_watchdog_kick(void);

/** @brief Reboot the board through the watchdog reset path. */
void system_reboot(void);

/**
 * @brief Schedule a Core 0 reboot while continuing to service USB until the deadline.
 * @param delay_ms Milliseconds before reboot, allowing the host to receive its result.
 */
void system_schedule_reboot(uint32_t delay_ms);

/** @brief Account Core 0 service work, excluding its idle sleep, in microseconds. */
void system_record_service(uint32_t elapsed_us);

/** @brief Return Core 0 service wall time in basis points of the last measurement window. */
uint16_t system_service_load(void);

/** @brief Read the internal die-temperature estimate in millidegrees Celsius; not calibrated ambient temperature. */
int32_t system_temperature_millic(void);

#endif