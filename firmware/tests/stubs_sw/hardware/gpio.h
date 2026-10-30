/**
 * @file gpio.h
 * SPDX-License-Identifier: BSD-3-Clause
 * @copyright Copyright (c) 2026 Ji Dong
 */

#ifndef TEST_SW_GPIO_H
#define TEST_SW_GPIO_H

enum { GPIO_IN = 0, GPIO_OUT = 1 };
uint32_t gpio_get_all(void);

void gpio_init(uint gpio);
void gpio_set_dir(uint gpio, int direction);
void gpio_put(uint gpio, int value);

extern int sw_test_gpio_level[32];

#endif
