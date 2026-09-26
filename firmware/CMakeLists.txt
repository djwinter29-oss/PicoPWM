cmake_minimum_required(VERSION 3.13)

# Set PICO_SDK_PATH environment variable or pass via -DPICO_SDK_PATH=...
include(pico_sdk_import.cmake)

project(PicoPWM C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_PWM_FIRMWARE_VERSION "0.0.0-dev" CACHE STRING "Firmware version string exposed over CDC and I2C")
set(PICO_PWM_PROFILE "generator" CACHE STRING "Firmware channel profile (generator or monitor)")
set_property(CACHE PICO_PWM_PROFILE PROPERTY STRINGS generator monitor software_generator software_monitor)

if(PICO_PWM_PROFILE STREQUAL "generator")
    set(PICO_PWM_PROFILE_SOURCE src/config/profiles/generator.c)
    set(PICO_PWM_BACKEND_SOURCES
        src/pwmdriver/generator/hardware_generator.c
        src/pwmdriver/generator/pio_generator.c
        src/pwmdriver/generator/software_generator.c
    )
elseif(PICO_PWM_PROFILE STREQUAL "monitor")
    set(PICO_PWM_PROFILE_SOURCE src/config/profiles/monitor.c)
    set(PICO_PWM_BACKEND_SOURCES
        src/pwmdriver/monitor/hardware_monitor.c
        src/pwmdriver/monitor/pio_monitor.c
        src/pwmdriver/monitor/software_monitor.c
    )
elseif(PICO_PWM_PROFILE STREQUAL "software_generator")
    set(PICO_PWM_PROFILE_SOURCE src/config/profiles/software_generator.c)
    set(PICO_PWM_BACKEND_SOURCES
        src/pwmdriver/generator/software_generator.c
    )
elseif(PICO_PWM_PROFILE STREQUAL "software_monitor")
    set(PICO_PWM_PROFILE_SOURCE src/config/profiles/software_monitor.c)
    set(PICO_PWM_BACKEND_SOURCES
        src/pwmdriver/monitor/software_monitor.c
    )
else()
    message(FATAL_ERROR "PICO_PWM_PROFILE must be generator, monitor, software_generator, or software_monitor")
endif()

pico_sdk_init()

add_executable(pico_pwm
    src/main.c
    src/control/control_iface.c
    src/config/pwm_profile.c
    ${PICO_PWM_PROFILE_SOURCE}
    src/pwmdriver/pwm_driver.c
    ${PICO_PWM_BACKEND_SOURCES}
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

if(PICO_PWM_PROFILE STREQUAL "generator")
    pico_generate_pio_header(pico_pwm
        ${CMAKE_CURRENT_LIST_DIR}/src/pwmdriver/generator/pio_generator.pio
    )
elseif(PICO_PWM_PROFILE STREQUAL "monitor")
    pico_generate_pio_header(pico_pwm
        ${CMAKE_CURRENT_LIST_DIR}/src/pwmdriver/monitor/pio_monitor.pio
    )
endif()

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

if(PICO_PWM_PROFILE STREQUAL "monitor")
    target_link_libraries(pico_pwm hardware_dma)
endif()

target_compile_definitions(pico_pwm PRIVATE
    PICO_PWM_FIRMWARE_VERSION_STR="${PICO_PWM_FIRMWARE_VERSION}"
)

if(PICO_PWM_PROFILE STREQUAL "monitor" OR PICO_PWM_PROFILE STREQUAL "software_monitor")
    target_compile_definitions(pico_pwm PRIVATE PICO_PWM_MONITOR_PROFILE=1)
endif()

if(PICO_PWM_PROFILE STREQUAL "software_generator" OR PICO_PWM_PROFILE STREQUAL "software_monitor")
    target_compile_definitions(pico_pwm PRIVATE PICO_PWM_SOFTWARE_PROFILE=1)
endif()

target_include_directories(pico_pwm PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/third_party/microrl
)

# USB CDC is handled directly through TinyUSB; UART is disabled.
pico_enable_stdio_usb(pico_pwm 0)
pico_enable_stdio_uart(pico_pwm 0)

pico_add_extra_outputs(pico_pwm)
