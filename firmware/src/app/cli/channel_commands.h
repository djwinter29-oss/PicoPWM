#ifndef CHANNEL_COMMANDS_H
#define CHANNEL_COMMANDS_H

#include "shell.h"

bool channel_commands_get(void *context, int argc, const char *const *argv);
bool channel_commands_set(void *context, int argc, const char *const *argv);
bool channel_commands_status(void *context, int argc, const char *const *argv);

#endif
