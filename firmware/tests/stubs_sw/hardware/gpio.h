#ifndef TEST_SW_GPIO_H
#define TEST_SW_GPIO_H

enum { GPIO_OUT = 1 };

void gpio_init(uint gpio);
void gpio_set_dir(uint gpio, int direction);
void gpio_put(uint gpio, int value);

extern int sw_test_gpio_level[32];

#endif
