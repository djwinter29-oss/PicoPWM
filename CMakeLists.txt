cmake_minimum_required(VERSION 3.13)

# Set PICO_SDK_PATH environment variable or pass via -DPICO_SDK_PATH=...
include(pico_sdk_import.cmake)

project(PicoPWM C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico_pwm
    src/main.c
    src/control.c
    src/hw_pwm.c
    src/sw_pwm.c
    src/cmd_parser.c
    src/cdc_cmd.c
    src/i2c_slave.c
)

target_link_libraries(pico_pwm
    pico_stdlib
    hardware_pwm
    hardware_timer
    hardware_irq
    hardware_clocks
    hardware_gpio
    hardware_i2c
    tinyusb_device
    tinyusb_board
)

# USB CDC is the serial command interface; UART is disabled.
pico_enable_stdio_usb(pico_pwm 1)
pico_enable_stdio_uart(pico_pwm 0)

pico_add_extra_outputs(pico_pwm)
