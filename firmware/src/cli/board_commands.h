#ifndef BOARD_COMMANDS_H
#define BOARD_COMMANDS_H

#include "shell.h"

bool board_commands_info(int argc, const char *const *argv);
bool board_commands_version(int argc, const char *const *argv);
bool board_commands_led(int argc, const char *const *argv);
bool board_commands_reboot(int argc, const char *const *argv);
bool board_commands_stop(int argc, const char *const *argv);

#endif
