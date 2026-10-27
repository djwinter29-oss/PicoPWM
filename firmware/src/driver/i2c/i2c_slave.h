#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <stdint.h>

void i2c_slave_init(uint8_t address);
void i2c_slave_poll(void);

/**
 * @brief Build a deferred I2C read response and release a stretched clock.
 *
 * Safe to call from Core 0 while a PWM mailbox wait holds the control lock.
 * It does not execute queued writes.
 */
void i2c_slave_service_reads(void);

#endif