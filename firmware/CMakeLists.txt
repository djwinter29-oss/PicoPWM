cmake_minimum_required(VERSION 3.13)

# The SDK is pinned by the firmware/third_party/pico-sdk submodule.
set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk" CACHE INTERNAL "")
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(PicoPWM C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_PWM_FIRMWARE_VERSION "0.0.0-dev" CACHE STRING "Firmware version string exposed over CDC, I2C and HID; releases pass the tag version")
include(${CMAKE_CURRENT_LIST_DIR}/version.cmake)

pico_sdk_init()

add_executable(pico_pwm
    src/main.c
    src/device.c
    src/platform/boot_config.c
    src/platform/led.c
    src/platform/system.c
    src/driver/pwm/pwm_driver.c
    src/driver/pwm/hardware/hardware_generator.c
    src/driver/pwm/pio/pio_generator.c
    src/driver/pwm/software/software_generator.c
    src/driver/pwm/hardware/hardware_monitor.c
    src/driver/pwm/pio/pio_monitor.c
    src/driver/pwm/software/software_monitor.c
    src/app/cli/shell.c
    src/app/cli/pwm_commands.c
    src/app/cli/channel_commands.c
    src/app/cli/board_commands.c
    third_party/embedded-cli/lib/src/embedded_cli.c
    src/config/board.c
    src/app/i2c/i2c_control_map.c
    src/app/usb/usb_cdc.c
    src/app/usb/usb_hid.c
    src/app/usb/hid_protocol.c
    src/app/usb/usb_descriptors.c
    src/driver/i2c/i2c_slave.c
    src/driver/i2c/i2c_slave_hw.c
)

pico_generate_pio_header(pico_pwm
    ${CMAKE_CURRENT_LIST_DIR}/src/driver/pwm/pio/pio_generator.pio
)
pico_generate_pio_header(pico_pwm
    ${CMAKE_CURRENT_LIST_DIR}/src/driver/pwm/pio/pio_monitor.pio
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

target_link_libraries(pico_pwm hardware_dma hardware_flash)

target_compile_definitions(pico_pwm PRIVATE
    PICO_PWM_FIRMWARE_VERSION_STR="${PICO_PWM_FIRMWARE_VERSION}"
    PICO_PWM_BCD_DEVICE=${PICO_PWM_BCD_DEVICE}u
)

target_include_directories(pico_pwm PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/config
    ${CMAKE_CURRENT_LIST_DIR}/third_party/embedded-cli/lib/include
)

# USB CDC is handled directly through TinyUSB; UART is disabled.
pico_enable_stdio_usb(pico_pwm 0)
pico_enable_stdio_uart(pico_pwm 0)

pico_add_extra_outputs(pico_pwm)
include(${CMAKE_CURRENT_LIST_DIR}/config-storage.cmake)
