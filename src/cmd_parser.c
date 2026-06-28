#include "cmd_parser.h"
#include "control.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define CMD_BUF_SIZE 128

static char cmd_buffer[CMD_BUF_SIZE];
static int cmd_len = 0;

void print_status(void) {
    printf("\r\n=== All PWM channels (logical 0..23) ===\r\n");
    printf("Ch  Type  State  Freq(Hz)   Duty(%%)   Pulses\r\n");
    for (int i = 0; i < CONTROL_CHANNEL_COUNT; i++) {
        const char *type = (i < HW_PWM_COUNT) ? "HW" : "SW";
        printf("%-2d  %-4s  %-3s  %9.3f  %6.1f  %lu\r\n",
               i,
               type,
               control_is_enabled(i) ? "ON" : "OFF",
               control_get_freq(i),
               control_get_duty(i) * 100.0f,
               (unsigned long)control_get_pulse_count(i));
    }
    printf("\r\n");
}

void print_help(void) {
    printf("\r\nUnified control interface: each channel has freq, duty, pulse_count.\r\n");
    printf("Logical channels: 0..7 = hardware PWM, 8..23 = software PWM.\r\n\r\n");
    printf("Commands:\r\n");
    printf("  info                    Show device type\r\n");
    printf("  version                 Show firmware version\r\n");
    printf("  get <ch>                Read channel properties (ch=0..23)\r\n");
    printf("  set <ch> f <freq>       Set frequency (Hz, 0 = off)\r\n");
    printf("  set <ch> d <duty%%>      Set duty cycle (0..100)\r\n");
    printf("  h <ch> <freq> <duty%%>   Set HW PWM channel (ch=0..7)\r\n");
    printf("  s <ch> <freq> <duty%%>   Set SW PWM channel (ch=0..15)\r\n");
    printf("  d <ch> <duty%%>          Set SW PWM duty only (ch=0..15)\r\n");
    printf("  stop                    Stop all channels and reset to power-up defaults\r\n");
    printf("  status                  Show all channels\r\n");
    printf("  help                    Show this help\r\n");
    printf("\r\nNotes:\r\n");
    printf("  pulse_count is read-only. It can only be cleared by the stop command.\r\n");
    printf("\r\nExamples:\r\n");
    printf("  get 0                   -> read channel 0\r\n");
    printf("  set 0 f 1000            -> HW0 1000 Hz, keeps 50%% duty\r\n");
    printf("  set 8 d 25              -> SW8 25%% duty\r\n");
    printf("  h 0 1000 50             -> HW0 1000 Hz, 50%% duty\r\n");
    printf("  s 0 100 25              -> SW0 100 Hz, 25%% duty\r\n");
    printf("\r\n");
}

static void execute_command(char *cmd) {
    while (isspace((int)*cmd)) cmd++;
    if (*cmd == '\0') return;

    char *saveptr;
    char *token = strtok_r(cmd, " \t", &saveptr);
    if (token == NULL) return;

    if (strcmp(token, "info") == 0) {
        printf("PicoPWM\r\n");

    } else if (strcmp(token, "version") == 0) {
        printf("1.0.0\r\n");

    } else if (strcmp(token, "get") == 0) {
        char *ch_str = strtok_r(NULL, " \t", &saveptr);
        if (ch_str) {
            int ch = atoi(ch_str);
            if (ch >= 0 && ch < CONTROL_CHANNEL_COUNT) {
                printf("CH%d: freq=%.3f Hz, duty=%.1f%%, pulses=%lu, enabled=%s\r\n",
                       ch,
                       control_get_freq(ch),
                       control_get_duty(ch) * 100.0f,
                       (unsigned long)control_get_pulse_count(ch),
                       control_is_enabled(ch) ? "yes" : "no");
            } else {
                printf("ERR channel %d invalid (0..23)\r\n", ch);
            }
        } else {
            printf("ERR usage: get <ch>\r\n");
        }

    } else if (strcmp(token, "set") == 0) {
        char *ch_str = strtok_r(NULL, " \t", &saveptr);
        char *prop = strtok_r(NULL, " \t", &saveptr);
        char *val_str = strtok_r(NULL, " \t", &saveptr);
        if (ch_str && prop && val_str) {
            int ch = atoi(ch_str);
            float val = atof(val_str);
            if (ch < 0 || ch >= CONTROL_CHANNEL_COUNT) {
                printf("ERR channel %d invalid (0..23)\r\n", ch);
                return;
            }
            if (strcmp(prop, "f") == 0 || strcmp(prop, "freq") == 0) {
                if (control_set_freq(ch, val)) {
                    printf("OK CH%d freq=%.3f Hz\r\n", ch, val);
                } else {
                    printf("ERR CH%d freq invalid\r\n", ch);
                }
            } else if (strcmp(prop, "d") == 0 || strcmp(prop, "duty") == 0) {
                control_set_duty(ch, val / 100.0f);
                printf("OK CH%d duty=%.1f%%\r\n", ch, val);
            } else {
                printf("ERR unknown property '%s' (f/d only)\r\n", prop);
            }
        } else {
            printf("ERR usage: set <ch> <f|d> <value>\r\n");
        }

    } else if (strcmp(token, "reset") == 0) {
        printf("ERR reset command is disabled; use 'stop' to reset all channels\r\n");

    } else if (strcmp(token, "h") == 0) {
        char *ch_str = strtok_r(NULL, " \t", &saveptr);
        char *freq_str = strtok_r(NULL, " \t", &saveptr);
        char *duty_str = strtok_r(NULL, " \t", &saveptr);
        if (ch_str && freq_str && duty_str) {
            int ch = atoi(ch_str);
            float freq = atof(freq_str);
            float duty = atof(duty_str) / 100.0f;
            if (ch >= 0 && ch < HW_PWM_COUNT &&
                control_set_freq(ch, freq) &&
                control_set_duty(ch, duty)) {
                printf("OK HW%d: %.3f Hz, %.1f%%\r\n", ch, freq, duty * 100.0f);
            } else {
                printf("ERR HW%d invalid (ch=0..7, duty=0..100)\r\n", ch);
            }
        } else {
            printf("ERR usage: h <ch> <freq> <duty%%>\r\n");
        }

    } else if (strcmp(token, "s") == 0) {
        char *ch_str = strtok_r(NULL, " \t", &saveptr);
        char *freq_str = strtok_r(NULL, " \t", &saveptr);
        char *duty_str = strtok_r(NULL, " \t", &saveptr);
        if (ch_str && freq_str && duty_str) {
            int ch = atoi(ch_str);
            float freq = atof(freq_str);
            float duty = atof(duty_str) / 100.0f;
            int logical = ch + HW_PWM_COUNT;
            if (ch >= 0 && ch < SW_PWM_COUNT &&
                control_set_freq(logical, freq) &&
                control_set_duty(logical, duty)) {
                printf("OK SW%d: %.3f Hz, %.1f%%\r\n", ch, freq, duty * 100.0f);
            } else {
                printf("ERR SW%d invalid (ch=0..15, duty=0..100)\r\n", ch);
            }
        } else {
            printf("ERR usage: s <ch> <freq> <duty%%>\r\n");
        }

    } else if (strcmp(token, "d") == 0) {
        char *ch_str = strtok_r(NULL, " \t", &saveptr);
        char *duty_str = strtok_r(NULL, " \t", &saveptr);
        if (ch_str && duty_str) {
            int ch = atoi(ch_str);
            float duty = atof(duty_str) / 100.0f;
            int logical = ch + HW_PWM_COUNT;
            if (ch >= 0 && ch < SW_PWM_COUNT) {
                control_set_duty(logical, duty);
                printf("OK SW%d duty: %.1f%%\r\n", ch, duty * 100.0f);
            } else {
                printf("ERR SW%d invalid (ch=0..15)\r\n", ch);
            }
        } else {
            printf("ERR usage: d <ch> <duty%%>\r\n");
        }

    } else if (strcmp(token, "stop") == 0) {
        control_stop_all();
        printf("OK all channels stopped and reset (freq=0, duty=50%%, pulses=0)\r\n");

    } else if (strcmp(token, "status") == 0 || strcmp(token, "st") == 0) {
        print_status();

    } else if (strcmp(token, "help") == 0) {
        print_help();

    } else {
        printf("ERR unknown command: '%s'. Type 'help'.\r\n", token);
    }
}

void cmd_parser_process_char(char c) {
    if (c == '\r' || c == '\n') {
        cmd_buffer[cmd_len] = '\0';
        if (cmd_len > 0) {
            execute_command(cmd_buffer);
            cmd_len = 0;
        }
    } else if (c == '\b' || c == 0x7f) {
        if (cmd_len > 0) {
            cmd_len--;
            printf("\b \b");
        }
    } else if (cmd_len < CMD_BUF_SIZE - 1) {
        cmd_buffer[cmd_len++] = c;
    } else {
        cmd_len = 0;
        printf("\r\nERR command too long\r\n");
    }
}

void cmd_parser_init(void) {
    cmd_len = 0;
}

void cmd_parser_poll(void) {
    // No-op; transport-specific code feeds characters.
}
