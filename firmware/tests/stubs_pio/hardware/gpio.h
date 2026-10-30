/**
 * @file gpio.h
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#ifndef TEST_PIO_GPIO_H
#define TEST_PIO_GPIO_H

enum { GPIO_OUT = 1, GPIO_FUNC_SIO = 5 };

void gpio_set_function(uint gpio, int function);
void gpio_set_dir(uint gpio, int direction);
void gpio_put(uint gpio, int value);

extern int pio_test_gpio_level[32];

#endif
