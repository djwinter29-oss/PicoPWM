# Validates PICO_PWM_FIRMWARE_VERSION and computes PICO_PWM_BCD_DEVICE.
# Release builds receive MAJOR.MINOR.PATCH from the tag (tools/release/resolve-release-version.sh);
# local builds default to 0.0.0-dev. Runs standalone with `cmake -DPICO_PWM_FIRMWARE_VERSION=1.2.3 -P`.
if(NOT PICO_PWM_FIRMWARE_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-(dev|rc[1-9][0-9]*))?$")
    message(FATAL_ERROR
        "PICO_PWM_FIRMWARE_VERSION must be MAJOR.MINOR.PATCH with optional -dev or -rcN "
        "(got '${PICO_PWM_FIRMWARE_VERSION}')")
endif()
set(_pwm_major "${CMAKE_MATCH_1}")
set(_pwm_minor "${CMAKE_MATCH_2}")
set(_pwm_patch "${CMAKE_MATCH_3}")
if(_pwm_major GREATER 99 OR _pwm_minor GREATER 99 OR _pwm_patch GREATER 255)
    message(FATAL_ERROR "PICO_PWM_FIRMWARE_VERSION: major/minor must be 0-99 and patch 0-255")
endif()

# USB bcdDevice carries major.minor only (1.2.3 -> 0x0102).
math(EXPR PICO_PWM_BCD_DEVICE
    "((${_pwm_major} / 10) * 4096) + ((${_pwm_major} % 10) * 256) + ((${_pwm_minor} / 10) * 16) + (${_pwm_minor} % 10)")

if(CMAKE_SCRIPT_MODE_FILE)
    message("RESULT bcd=${PICO_PWM_BCD_DEVICE}")
endif()
