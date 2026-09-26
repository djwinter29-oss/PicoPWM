cmake_minimum_required(VERSION 3.13)

# Set PICO_SDK_PATH environment variable or pass via -DPICO_SDK_PATH=...
include(pico_sdk_import.cmake)

project(PicoPWM C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_PWM_FIRMWARE_VERSION "0.0.0-dev" CACHE STRING "Firmware version string exposed over CDC and I2C")
set(PICO_PWM_PROFILE "generator" CACHE STRING "Firmware channel profile")
set_property(CACHE PICO_PWM_PROFILE PROPERTY STRINGS generator monitor 112 121 122 211 212 221)
set(PICO_PWM_I2C_CLOCK_SPEED 400000 CACHE STRING "I2C slave clock speed in Hz (100000 or 400000)")
set(PICO_PWM_I2C_ADDR 0x40 CACHE STRING "I2C slave address (7-bit, 0x00..0x7F)")

if(PICO_PWM_PROFILE STREQUAL "generator")
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/profile_111.c)
elseif(PICO_PWM_PROFILE STREQUAL "monitor")
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/profile_222.c)
elseif(PICO_PWM_PROFILE STREQUAL "112")
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/profile_112.c)
elseif(PICO_PWM_PROFILE STREQUAL "121")
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/profile_121.c)
elseif(PICO_PWM_PROFILE STREQUAL "122")
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/profile_122.c)
elseif(PICO_PWM_PROFILE STREQUAL "211")
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/profile_211.c)
elseif(PICO_PWM_PROFILE STREQUAL "212")
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/profile_212.c)
elseif(PICO_PWM_PROFILE STREQUAL "221")
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/profile_221.c)
else()
    message(FATAL_ERROR "PICO_PWM_PROFILE must be one of: generator, monitor, 112, 121, 122, 211, 212, 221")
endif()

pico_sdk_init()

add_executable(pico_pwm
    src/main.c
    src/control/control_iface.c
    src/profile/pwm_profile.c
    ${PICO_PWM_PROFILE_SOURCE}
    src/driver/led.c
    src/driver/system.c
    src/pwmdriver/pwm_driver.c
    src/pwmdriver/generator/hardware_generator.c
    src/pwmdriver/generator/pio_generator.c
    src/pwmdriver/generator/software_generator.c
    src/pwmdriver/monitor/hardware_monitor.c
    src/pwmdriver/monitor/pio_monitor.c
    src/pwmdriver/monitor/software_monitor.c
    src/cli/shell.c
    src/cli/pwm_commands.c
    src/cli/pwm_channel_commands.c
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

target_link_libraries(pico_pwm hardware_dma)

target_compile_definitions(pico_pwm PRIVATE
    PICO_PWM_FIRMWARE_VERSION_STR="${PICO_PWM_FIRMWARE_VERSION}"
    PICO_PWM_PROFILE_STR="${PICO_PWM_PROFILE}"
    PICO_PWM_I2C_CLOCK_SPEED=${PICO_PWM_I2C_CLOCK_SPEED}
    PICO_PWM_I2C_ADDR=${PICO_PWM_I2C_ADDR}
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
