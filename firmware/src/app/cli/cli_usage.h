/**
 * @file cli_usage.h
 * @brief Shared user-facing CDC command syntax.
 */

#ifndef CLI_USAGE_H
#define CLI_USAGE_H

/** @brief User-facing syntax for reading PWM channel state. */
#define CLI_SYNTAX_PWM_GET "pwm get <channel|all>"
/** @brief Usage response for reading PWM channel state. */
#define CLI_USAGE_PWM_GET "Usage: " CLI_SYNTAX_PWM_GET

/** @brief User-facing syntax for setting one PWM channel. */
#define CLI_SYNTAX_PWM_SET "pwm set <channel> <freq_hz> [duty_percent]"
/** @brief Usage response for setting one PWM channel. */
#define CLI_USAGE_PWM_SET "Usage: " CLI_SYNTAX_PWM_SET

/** @brief User-facing syntax for stopping generator outputs. */
#define CLI_SYNTAX_PWM_STOP "pwm stop all"
/** @brief Usage response for stopping generator outputs. */
#define CLI_USAGE_PWM_STOP "Usage: " CLI_SYNTAX_PWM_STOP

/** @brief User-facing syntax for reading board state. */
#define CLI_SYNTAX_BOARD_GET "board get <clock|led|version>"
/** @brief Usage response for reading board state. */
#define CLI_USAGE_BOARD_GET "Usage: " CLI_SYNTAX_BOARD_GET

/** @brief User-facing syntax for setting the board LED. */
#define CLI_SYNTAX_BOARD_SET "board set led <on|off>"
/** @brief Usage response for setting the board LED. */
#define CLI_USAGE_BOARD_SET "Usage: " CLI_SYNTAX_BOARD_SET

/** @brief User-facing syntax for rebooting the board. */
#define CLI_SYNTAX_BOARD_REBOOT "board reboot"
/** @brief Usage response for rebooting the board. */
#define CLI_USAGE_BOARD_REBOOT "Usage: " CLI_SYNTAX_BOARD_REBOOT

/** @brief User-facing syntax for reading startup configuration. */
#define CLI_SYNTAX_CONFIG_GET "config get [banka|bankb|bankc|clock|address]"

/** @brief User-facing syntax for updating startup configuration. */
#define CLI_SYNTAX_CONFIG_SET "config set <key=value>[, <key=value>...]"
/** @brief Usage response including accepted configuration values. */
#define CLI_USAGE_CONFIG_SET \
    "Usage: " CLI_SYNTAX_CONFIG_SET \
    "; banka/bankb/bankc=generator|monitor, clock=default|150|200|250, address=8..119"

#endif
