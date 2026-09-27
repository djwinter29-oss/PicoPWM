cmake_minimum_required(VERSION 3.13)

# Set PICO_SDK_PATH in the environment or pass -DPICO_SDK_PATH=... to CMake.
if(NOT DEFINED PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
if(NOT DEFINED PICO_SDK_PATH OR PICO_SDK_PATH STREQUAL "")
    message(FATAL_ERROR "PICO_SDK_PATH must point to an installed Pico SDK")
endif()
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(PicoPWM C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_PWM_FIRMWARE_VERSION "0.0.0-dev" CACHE STRING "Firmware version string exposed over CDC and I2C")
set(PICO_PWM_I2C_CLOCK_SPEED 400000 CACHE STRING "I2C slave clock speed in Hz (100000 or 400000)")

pico_sdk_init()

add_executable(pico_pwm
    src/main.c
    src/device_api/device_api.c
    src/pwmdriver/pwm_driver_config.c
    src/board/led.c
    src/board/system.c
    src/pwmdriver/pwm_driver.c
    src/pwmdriver/generator/hardware_generator.c
    src/pwmdriver/generator/pio_generator.c
    src/pwmdriver/generator/software_generator.c
    src/pwmdriver/monitor/hardware_monitor.c
    src/pwmdriver/monitor/pio_monitor.c
    src/pwmdriver/monitor/software_monitor.c
    src/cli/shell.c
    src/cli/pwm_commands.c
    src/cli/channel_commands.c
    src/cli/board_commands.c
    third_party/microrl/microrl.c
    src/i2c/i2c_control_map.c
    src/usb/usb_cdc.c
    src/usb/usb_descriptors.c
    src/i2c/i2c_slave.c
)

pico_generate_pio_header(pico_pwm
    ${CMAKE_CURRENT_LIST_DIR}/src/pwmdriver/generator/pio_generator.pio
)
pico_generate_pio_header(pico_pwm
    ${CMAKE_CURRENT_LIST_DIR}/src/pwmdriver/monitor/pio_monitor.pio
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
    PICO_PWM_I2C_CLOCK_SPEED=${PICO_PWM_I2C_CLOCK_SPEED}
)

target_include_directories(pico_pwm PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/config
    ${CMAKE_CURRENT_LIST_DIR}/third_party/microrl
)

# USB CDC is handled directly through TinyUSB; UART is disabled.
pico_enable_stdio_usb(pico_pwm 0)
pico_enable_stdio_uart(pico_pwm 0)

pico_add_extra_outputs(pico_pwm)
