#ifndef BOARD_COMMANDS_H
#define BOARD_COMMANDS_H

#include "shell.h"

bool board_commands_info(void *context, int argc, const char *const *argv);
bool board_commands_version(void *context, int argc, const char *const *argv);
bool board_commands_bank(void *context, int argc, const char *const *argv);
bool board_commands_led(void *context, int argc, const char *const *argv);
bool board_commands_reboot(void *context, int argc, const char *const *argv);
bool board_commands_stop(void *context, int argc, const char *const *argv);

#endif
