#ifndef CMD_PARSER_H
#define CMD_PARSER_H

void cmd_parser_init(void);
void cmd_parser_process_char(char c);
void cmd_parser_poll(void);

void print_help(void);
void print_status(void);

#endif
