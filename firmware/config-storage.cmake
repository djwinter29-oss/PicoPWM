# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

# Reserve the final flash sector for persistent configuration. The SDK-resolved flash region
# (board header and default overrides included) gives the physical capacity; the linker script
# override shrinks the program region so the image can never overlap the sector.
set(_sdk_flash_region "${CMAKE_BINARY_DIR}/pico-sdk/src/rp2_common/pico_standard_link/pico_flash_region.ld")
file(READ "${_sdk_flash_region}" _flash_region)
string(REGEX MATCH "LENGTH = ([^\n]+)" _flash_length "${_flash_region}")
if(NOT _flash_length)
    message(FATAL_ERROR "Cannot resolve board flash capacity from ${_sdk_flash_region}")
endif()
math(EXPR PICO_PWM_CONFIG_FLASH_SIZE "${CMAKE_MATCH_1}")
math(EXPR PICO_PWM_CONFIG_FLASH_OFFSET "${PICO_PWM_CONFIG_FLASH_SIZE} - 4096")
math(EXPR _flash_alignment "${PICO_PWM_CONFIG_FLASH_SIZE} % 4096")
if(PICO_PWM_CONFIG_FLASH_SIZE LESS 8192 OR NOT _flash_alignment EQUAL 0)
    message(FATAL_ERROR "Persistent configuration requires a sector-aligned board flash capacity of at least 8192 bytes")
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/config-link")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/config-link/pico_flash_region.ld"
    "FLASH(rx) : ORIGIN = 0x10000000, LENGTH = ${PICO_PWM_CONFIG_FLASH_OFFSET}\n")
pico_add_linker_script_override_path(pico_pwm "${CMAKE_CURRENT_BINARY_DIR}/config-link" FILES pico_flash_region.ld)
target_compile_definitions(pico_pwm PRIVATE PICO_PWM_CONFIG_FLASH_OFFSET=${PICO_PWM_CONFIG_FLASH_OFFSET}u)
