#ifndef TEST_HARDWARE_FLASH_H
#define TEST_HARDWARE_FLASH_H

#include <stddef.h>
#include <stdint.h>

#define FLASH_SECTOR_SIZE 4096u
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#define TEST_FLASH_SIZE (2u * FLASH_SECTOR_SIZE)
extern uint8_t test_flash[TEST_FLASH_SIZE];
#define XIP_BASE ((uintptr_t)test_flash)

void test_flash_reset(void);
void flash_range_erase(uint32_t offset, size_t count);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count);

#endif
