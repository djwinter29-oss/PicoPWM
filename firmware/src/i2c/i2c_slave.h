#ifndef I2C_SLAVE_H
#define I2C_SLAVE_H

#include <stdint.h>

void i2c_slave_init(uint8_t address);
void i2c_slave_poll(void);

#endif