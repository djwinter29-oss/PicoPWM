#ifndef TEST_HARDWARE_FLASH_H
#define TEST_HARDWARE_FLASH_H

#include <stddef.h>
#include <stdint.h>

#define FLASH_SECTOR_SIZE 4096u
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#define XIP_BASE 0u

static inline void flash_range_erase(uint32_t offset, size_t count) {
    (void)offset;
    (void)count;
}

static inline void flash_range_program(uint32_t offset, const uint8_t *data, size_t count) {
    (void)offset;
    (void)data;
    (void)count;
}

#endif
