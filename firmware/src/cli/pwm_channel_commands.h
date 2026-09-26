#ifndef PWM_CHANNEL_COMMANDS_H
#define PWM_CHANNEL_COMMANDS_H

#include "shell.h"

bool pwm_channel_commands_get(void *context, int argc, const char *const *argv);
bool pwm_channel_commands_set(void *context, int argc, const char *const *argv);
bool pwm_channel_commands_status(void *context, int argc, const char *const *argv);

#endif
