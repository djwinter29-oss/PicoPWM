if(NOT EXISTS "${PICO_PWM_BINARY}")
    message(FATAL_ERROR "Firmware binary not found: ${PICO_PWM_BINARY}")
endif()

file(SIZE "${PICO_PWM_BINARY}" binary_size)
if(binary_size GREATER PICO_PWM_FLASH_LIMIT)
    message(FATAL_ERROR "Firmware binary (${binary_size} bytes) overlaps the reserved PWM configuration flash area starting at ${PICO_PWM_FLASH_LIMIT}")
endif()

message(STATUS "PWM configuration flash reservation is clear: binary=${binary_size}, limit=${PICO_PWM_FLASH_LIMIT}")
