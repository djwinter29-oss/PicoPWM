cmake_minimum_required(VERSION 3.13)

# Set PICO_SDK_PATH environment variable or pass via -DPICO_SDK_PATH=...
include(pico_sdk_import.cmake)

project(PicoPWM C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico_pwm
    src/main.c
    src/control/control_iface.c
    src/pwmdriver/pwm_driver.c
    src/pwmdriver/hw/generator.c
    src/pwmdriver/hw/monitor.c
    src/pwmdriver/sw_pwm_driver.c
    src/pwmdriver/pio/generator.c
    src/cli/cli_shell.c
    src/cli/device_cli.c
    src/i2c/i2c_control_map.c
    src/usb/usb_cdc.c
    src/usb/usb_descriptors.c
    src/i2c/i2c_slave.c
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

# USB CDC is handled directly through TinyUSB; UART is disabled.
pico_enable_stdio_usb(pico_pwm 0)
pico_enable_stdio_uart(pico_pwm 0)

pico_add_extra_outputs(pico_pwm)
