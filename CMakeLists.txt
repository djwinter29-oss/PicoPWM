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
    src/pwmdriver/hw_pwm_driver.c
    src/pwmdriver/sw_pwm_driver.c
    src/pwmdriver/pio/generator.c
    src/pwm_core.c
    src/cmd_parser.c
    src/cdc_cmd.c
    src/i2c_slave.c
)

pico_generate_pio_header(pico_pwm
    ${CMAKE_CURRENT_LIST_DIR}/src/pwmdriver/pio/generator.pio
)

target_link_libraries(pico_pwm
    pico_stdlib
    pico_multicore
    hardware_pwm
    hardware_timer
    hardware_irq
    hardware_clocks
    hardware_gpio
    hardware_i2c
    hardware_pio
    hardware_sync
    tinyusb_device
    tinyusb_board
)

# USB CDC is the serial command interface; UART is disabled.
pico_enable_stdio_usb(pico_pwm 1)
pico_enable_stdio_uart(pico_pwm 0)

pico_add_extra_outputs(pico_pwm)
