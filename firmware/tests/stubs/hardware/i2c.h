#ifndef TEST_STUB_HARDWARE_I2C_H
#define TEST_STUB_HARDWARE_I2C_H

#include "pico/stdlib.h"

typedef struct {
    int unused;
} i2c_inst_t;

extern i2c_inst_t i2c1_inst;

#define i2c1 (&i2c1_inst)
#define I2C1_IRQ 24

#endif
