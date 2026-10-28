#ifndef TEST_CDC_TUSB_H
#define TEST_CDC_TUSB_H

#include <stdbool.h>
#include <stdint.h>

void tusb_init(void);
void tud_task(void);
bool tud_ready(void);
bool tud_cdc_n_connected(uint8_t itf);
uint32_t tud_cdc_n_available(uint8_t itf);
uint32_t tud_cdc_n_read(uint8_t itf, void *buffer, uint32_t length);
uint32_t tud_cdc_n_write_available(uint8_t itf);
uint32_t tud_cdc_n_write(uint8_t itf, const void *buffer, uint32_t length);
void tud_cdc_n_write_flush(uint8_t itf);

void cdc_test_set_ready(bool ready);
void cdc_test_load_rx(const uint8_t *data, uint32_t length);
uint32_t cdc_test_rx_remaining(void);
void cdc_test_set_tx_space(uint32_t space);

#endif
