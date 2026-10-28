#ifndef TEST_PIO_GENERATOR_PIO_H
#define TEST_PIO_GENERATOR_PIO_H

#include "hardware/pio.h"

typedef struct pio_program {
    int unused;
} pio_program_t;

static const pio_program_t generator_program = {0};

static inline void generator_program_init(PIO pio, uint sm, uint offset, uint pin) {
    (void)pio;
    (void)sm;
    (void)offset;
    (void)pin;
}

#endif
