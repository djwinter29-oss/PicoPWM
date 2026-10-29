#ifndef TEST_PIO_PIO_H
#define TEST_PIO_PIO_H

#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

typedef struct pio_hw {
    int id;
} pio_hw_t;
typedef pio_hw_t *PIO;

extern PIO pio0;
extern PIO pio1;
extern bool pio_test_tx_fifo_full;
extern unsigned pio_test_puts;
extern bool pio_test_enabled[2][4];
extern uint pio_test_pc[2][4];
extern uint pio_test_fifo_count[2][4];
extern unsigned pio_test_starts;

static inline void pio_gpio_init(PIO pio, uint pin) {
    (void)pio;
    (void)pin;
}
static inline void pio_sm_set_enabled(PIO pio, uint sm, bool enabled) {
    if (enabled) {
        assert(pio_test_pc[pio->id][sm] == (uint)(pio->id + 3));
        assert(pio_test_fifo_count[pio->id][sm] == 2u);
        pio_test_pc[pio->id][sm] += 5u;
        pio_test_fifo_count[pio->id][sm] = 0u;
        pio_test_starts++;
    }
    pio_test_enabled[pio->id][sm] = enabled;
}
static inline void pio_sm_clear_fifos(PIO pio, uint sm) {
    pio_test_fifo_count[pio->id][sm] = 0u;
}
static inline uint pio_encode_jmp(uint address) {
    return address;
}
static inline void pio_sm_exec(PIO pio, uint sm, uint instruction) {
    assert(!pio_test_enabled[pio->id][sm]);
    pio_test_pc[pio->id][sm] = instruction;
}
static inline void pio_sm_restart(PIO pio, uint sm) {
    assert(!pio_test_enabled[pio->id][sm]);
}
static inline void pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint pin, uint count, bool out) {
    (void)pio;
    (void)sm;
    (void)pin;
    (void)count;
    (void)out;
}
static inline void pio_sm_set_clkdiv_int_frac(PIO pio, uint sm, uint16_t div_int, uint8_t div_frac) {
    assert(!pio_test_enabled[pio->id][sm]);
    (void)div_int;
    (void)div_frac;
}
static inline uint pio_add_program(PIO pio, const void *program) {
    (void)program;
    return (uint)(pio->id + 3);
}
static inline bool pio_sm_is_tx_fifo_full(PIO pio, uint sm) {
    (void)pio;
    (void)sm;
    return pio_test_tx_fifo_full;
}
static inline void pio_sm_put(PIO pio, uint sm, uint32_t value) {
    (void)value;
    pio_test_fifo_count[pio->id][sm]++;
    pio_test_puts += 1u;
}

#endif
