cmake_minimum_required(VERSION 3.13)

# Set PICO_SDK_PATH environment variable or pass via -DPICO_SDK_PATH=...
include(pico_sdk_import.cmake)

project(PicoPWM C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_PWM_FIRMWARE_VERSION "0.0.0-dev" CACHE STRING "Firmware version string exposed over CDC and I2C")
set(PICO_PWM_PROFILE "generator" CACHE STRING "Firmware channel profile")
set_property(CACHE PICO_PWM_PROFILE PROPERTY STRINGS generator monitor software_generator software_monitor mixed)

if(PICO_PWM_PROFILE STREQUAL "generator")
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/generator.c)
elseif(PICO_PWM_PROFILE STREQUAL "monitor")
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/monitor.c)
elseif(PICO_PWM_PROFILE STREQUAL "software_generator")
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/software_generator.c)
elseif(PICO_PWM_PROFILE STREQUAL "software_monitor")
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/software_monitor.c)
elseif(PICO_PWM_PROFILE STREQUAL "mixed")
    if(PICO_BOARD MATCHES "^(pico|pico2|rp2040|rp2350)$")
        message(FATAL_ERROR "PICO_PWM_PROFILE=mixed requires a board exposing GPIO 23 and GPIO 24")
    endif()
    set(PICO_PWM_PROFILE_SOURCE src/profile/profiles/pio_sw_gen_sw_mon.c)
else()
    message(FATAL_ERROR "PICO_PWM_PROFILE must be generator, monitor, software_generator, software_monitor, or mixed")
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
