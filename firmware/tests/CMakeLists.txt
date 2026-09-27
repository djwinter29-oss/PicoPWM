cmake_minimum_required(VERSION 3.13)

project(PicoPWMHostTests C)

include(CTest)

if(NOT BUILD_TESTING)
    return()
endif()

set(TEST_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(SOURCE_ROOT ${CMAKE_CURRENT_LIST_DIR}/../src)
set(MICRORL_ROOT ${CMAKE_CURRENT_LIST_DIR}/../third_party/microrl)
set(TEST_STUB_ROOT ${TEST_ROOT}/stubs)

function(pico_pwm_test_warnings target)
    target_compile_features(${target} PRIVATE c_std_11)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wno-sign-compare)
endfunction()

add_executable(cli_shell_test
    cli_shell_test.c
    ${SOURCE_ROOT}/cli/shell.c
    ${MICRORL_ROOT}/microrl.c
)
target_include_directories(cli_shell_test PRIVATE
    ${SOURCE_ROOT}
    ${MICRORL_ROOT}
)
pico_pwm_test_warnings(cli_shell_test)
add_test(NAME cli_shell COMMAND cli_shell_test)

add_executable(cli_channel_commands_test
    cli_channel_commands_test.c
    ${SOURCE_ROOT}/cli/channel_commands.c
)
target_include_directories(cli_channel_commands_test PRIVATE
    ${TEST_STUB_ROOT}
    ${SOURCE_ROOT}
    ${MICRORL_ROOT}
)
pico_pwm_test_warnings(cli_channel_commands_test)
add_test(NAME cli_channel_commands COMMAND cli_channel_commands_test)

add_executable(channel_config_test
    channel_config_test.c
    ${SOURCE_ROOT}/pwmdriver/channel_config/channel_config.c
)
target_include_directories(channel_config_test PRIVATE
    ${TEST_STUB_ROOT}
    ${SOURCE_ROOT}
)
pico_pwm_test_warnings(channel_config_test)
add_test(NAME channel_config COMMAND channel_config_test)
