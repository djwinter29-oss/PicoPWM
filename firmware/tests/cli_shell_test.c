#include "app/cli/cli_shell.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const char *input;
    size_t input_offset;
    char output[2048];
    size_t output_length;
    size_t output_limit;
} test_transport_t;

typedef struct {
    shell_t *shell;
    uint32_t command_calls;
    int last_argc;
} test_context_t;

static uint32_t test_read(void *context, uint8_t *data, uint32_t capacity) {
    test_transport_t *transport = context;
    size_t remaining = strlen(transport->input) - transport->input_offset;
    size_t length = (remaining < capacity) ? remaining : capacity;

    memcpy(data, transport->input + transport->input_offset, length);
    transport->input_offset += length;
    return (uint32_t)length;
}

static bool test_write(void *context, const uint8_t *data, uint32_t length) {
    test_transport_t *transport = context;

    if ((transport->output_limit != 0u) && ((transport->output_length + length) > transport->output_limit)) {
        return false;
    }
    assert(transport->output_length + length < sizeof(transport->output));
    memcpy(transport->output + transport->output_length, data, length);
    transport->output_length += length;
    transport->output[transport->output_length] = '\0';
    return true;
}

static bool test_command(void *context, int argc, const char *const *argv) {
    test_context_t *test = context;

    assert(argc == 1);
    assert(strcmp(argv[0], "ping") == 0);
    test->command_calls++;
    test->last_argc = argc;
    return shell_write_line(test->shell, "handled");
}

static bool test_count_arguments(void *context, int argc, const char *const *argv) {
    test_context_t *test = context;

    (void)argv;
    test->last_argc = argc;
    test->command_calls++;
    return true;
}

static shell_config_t test_config(test_context_t *context, const shell_transport_t *transport) {
    static const shell_command_t commands[] = {
        {"ping", "ping", test_command},
    };

    return (shell_config_t){
        .transport = transport,
        .commands = commands,
        .command_count = 1u,
        .unknown_message = "unknown",
        .unknown_handler = NULL,
        .command_context = context,
    };
}

static void test_independent_sessions_and_line_endings(void) {
    test_transport_t transport_a = {.input = "ping\r"};
    test_transport_t transport_b = {.input = "ping\n"};
    shell_t shell_a;
    shell_t shell_b;
    test_context_t context_a = {.shell = &shell_a};
    test_context_t context_b = {.shell = &shell_b};
    shell_transport_t binding_a = {.read = test_read, .write = test_write, .context = &transport_a};
    shell_transport_t binding_b = {.read = test_read, .write = test_write, .context = &transport_b};
    shell_config_t config_a = test_config(&context_a, &binding_a);
    shell_config_t config_b = test_config(&context_b, &binding_b);

    shell_init(&shell_a, &config_a);
    shell_init(&shell_b, &config_b);
    shell_poll(&shell_a);
    shell_poll(&shell_b);

    assert(context_a.command_calls == 1u);
    assert(context_b.command_calls == 1u);
    assert(strstr(transport_a.output, "handled\r\n") != NULL);
    assert(strstr(transport_b.output, "handled\r\n") != NULL);
}

static void test_poll_dispatches_one_command(void) {
    test_transport_t transport = {.input = "ping\rping\r"};
    shell_t shell;
    test_context_t context = {.shell = &shell};
    shell_transport_t binding = {.read = test_read, .write = test_write, .context = &transport};
    shell_config_t config = test_config(&context, &binding);

    assert(shell_init(&shell, &config));
    shell_poll(&shell);
    assert(context.command_calls == 1u);

    shell_poll(&shell);
    assert(context.command_calls == 2u);
}

static bool test_args(void *context, int argc, const char *const *argv) {
    (void)context;
    assert(argc == 3);
    assert(strcmp(argv[0], "set") == 0);
    assert(strcmp(argv[1], "a") == 0);
    assert(strcmp(argv[2], "10") == 0);
    return true;
}

static void test_arguments(void) {
    static const shell_command_t commands[] = {{"set", "set", test_args}};
    test_transport_t transport = {.input = "set a  10\r"};
    shell_t shell;
    test_context_t context = {.shell = &shell};
    shell_transport_t binding = {.read = test_read, .write = test_write, .context = &transport};
    shell_config_t config = test_config(&context, &binding);

    config.commands = commands;
    shell_init(&shell, &config);
    shell_poll(&shell);
    assert(strstr(transport.output, "unknown") == NULL);
}

static void test_completion(void) {
    test_transport_t transport = {.input = "pi\t\r"};
    shell_t shell;
    test_context_t context = {.shell = &shell};
    shell_transport_t binding = {.read = test_read, .write = test_write, .context = &transport};
    shell_config_t config = test_config(&context, &binding);

    shell_init(&shell, &config);
    shell_poll(&shell);

    assert(context.command_calls == 1u);
}

static void test_overlong_line_is_truncated_at_documented_limit(void) {
    char input[SHELL_MAX_INPUT_LENGTH + sizeof(" bad\r")];
    test_transport_t transport = {0};
    shell_t shell;
    test_context_t context = {.shell = &shell};
    shell_transport_t binding = {.read = test_read, .write = test_write, .context = &transport};
    shell_config_t config = test_config(&context, &binding);

    memcpy(input, "ping", 4u);
    memset(input + 4u, ' ', SHELL_MAX_INPUT_LENGTH - 4u);
    memcpy(input + SHELL_MAX_INPUT_LENGTH, " bad\r", sizeof(" bad\r"));
    transport.input = input;

    assert(shell_init(&shell, &config));
    shell_poll(&shell);

    assert(context.command_calls == 1u);
    assert(strstr(transport.output, "handled\r\n") != NULL);
}

static void test_interleaved_escape_sequences(void) {
    test_transport_t transport_a = {.input = "\033"};
    test_transport_t transport_b = {.input = "ping\r"};
    shell_t shell_a;
    shell_t shell_b;
    test_context_t context_a = {.shell = &shell_a};
    test_context_t context_b = {.shell = &shell_b};
    shell_transport_t binding_a = {.read = test_read, .write = test_write, .context = &transport_a};
    shell_transport_t binding_b = {.read = test_read, .write = test_write, .context = &transport_b};
    shell_config_t config_a = test_config(&context_a, &binding_a);
    shell_config_t config_b = test_config(&context_b, &binding_b);

    shell_init(&shell_a, &config_a);
    shell_init(&shell_b, &config_b);
    shell_poll(&shell_a);
    shell_poll(&shell_b);

    transport_a.input = "[Dping\r";
    transport_a.input_offset = 0u;
    shell_poll(&shell_a);

    assert(context_a.command_calls == 1u);
    assert(context_b.command_calls == 1u);
}

static bool reject_write(void *context, const uint8_t *data, uint32_t length) {
    (void)context;
    (void)data;
    (void)length;
    return false;
}

static bool test_command_fails(void *context, int argc, const char *const *argv) {
    (void)context;
    (void)argc;
    (void)argv;
    return false;
}

static bool test_unknown_handled(void *context, const char *name) {
    (void)context;
    (void)name;
    return true;
}

static bool test_reenter(void *context, int argc, const char *const *argv) {
    test_context_t *test = context;

    (void)argc;
    (void)argv;
    shell_poll(test->shell);
    shell_prompt(test->shell);
    return true;
}

static void test_rejected_inputs(void) {
    shell_t shell;
    test_transport_t transport = {.input = "nope\r"};
    test_context_t context = {.shell = &shell};
    shell_transport_t binding = {.read = test_read, .write = test_write, .context = &transport};
    shell_transport_t rejecting = {.read = test_read, .write = reject_write, .context = &transport};
    shell_transport_t unread = {.read = NULL, .write = test_write, .context = &transport};
    shell_config_t config = test_config(&context, &binding);
    const shell_command_t nameless[] = {{NULL, "help", test_command}};
    const shell_command_t failing[] = {{"fail", "fail", test_command_fails}};
    const shell_command_t reenter[] = {{"re", "re", test_reenter}};
    const shell_command_t too_many[SHELL_MAX_COMMANDS + 1u] = {
        {.name = "ping", .help = "ping", .handler = test_command}};

    memset(&shell, 0, sizeof(shell));
    shell_init(NULL, &config);
    shell_init(&shell, NULL);
    shell_poll(NULL);
    shell_poll(&shell);
    shell_prompt(NULL);
    shell_prompt(&shell);
    assert(!shell_write(NULL, "x"));
    assert(!shell_write(&shell, NULL));

    config.transport = &rejecting;
    shell_init(&shell, &config);
    assert(!shell_write(&shell, "x"));
    assert(!shell_write_line(&shell, "x"));
    assert(shell_write(&shell, ""));

    config = test_config(&context, &binding);
    config.transport = &unread;
    assert(!shell_init(&shell, &config));
    assert(!shell.initialized);

    config = test_config(&context, &binding);
    config.commands = NULL;
    assert(!shell_init(&shell, &config));
    assert(!shell.initialized);

    config = test_config(&context, &binding);
    config.commands = nameless;
    assert(!shell_init(&shell, &config));
    assert(!shell.initialized);

    config = test_config(&context, &binding);
    config.commands = too_many;
    config.command_count = SHELL_MAX_COMMANDS + 1u;
    assert(!shell_init(&shell, &config));
    assert(!shell.initialized);

    config = test_config(&context, &binding);
    assert(shell_init(&shell, &config));
    shell_poll(&shell);
    assert(strstr(transport.output, "unknown") != NULL);
    assert(shell_write_line(&shell, NULL));

    config.unknown_handler = test_unknown_handled;
    transport.input = "nope\r";
    transport.input_offset = 0u;
    transport.output_length = 0u;
    transport.output[0] = '\0';
    assert(shell_init(&shell, &config));
    assert(shell_prompt(&shell));
    shell_poll(&shell);
    assert(strstr(transport.output, "unknown") == NULL);

    config = test_config(&context, &binding);
    config.commands = failing;
    transport.input = "fail\r";
    transport.input_offset = 0u;
    transport.output_length = 0u;
    transport.output[0] = '\0';
    assert(shell_init(&shell, &config));
    shell_poll(&shell);
    assert(strstr(transport.output, "ERR command handler failed") != NULL);

    config.commands = reenter;
    transport.input = "re\r";
    transport.input_offset = 0u;
    assert(shell_init(&shell, &config));
    shell_poll(&shell);
}

static void test_excess_arguments_are_rejected(void) {
    static const shell_command_t commands[] = {{"args", "args", test_count_arguments}};
    test_transport_t transport = {.input = "args a b c d e f g h\r"};
    shell_t shell;
    test_context_t context = {.shell = &shell};
    shell_transport_t binding = {.read = test_read, .write = test_write, .context = &transport};
    shell_config_t config = test_config(&context, &binding);

    config.commands = commands;
    shell_init(&shell, &config);
    shell_poll(&shell);

    assert(context.command_calls == 0u);
    assert(strstr(transport.output, "ERR too many command arguments") != NULL);
}

static void test_line_write_is_atomic(void) {
    test_transport_t transport = {.input = ""};
    shell_t shell;
    test_context_t context = {.shell = &shell};
    shell_transport_t binding = {.read = test_read, .write = test_write, .context = &transport};
    shell_config_t config = test_config(&context, &binding);

    assert(shell_init(&shell, &config));
    transport.output_limit = 5u;
    assert(!shell_write_line(&shell, "text"));
    assert(transport.output_length == 0u);
    assert(transport.output[0] == '\0');

    transport.output_limit = 6u;
    assert(shell_write_line(&shell, "text"));
    assert(strcmp(transport.output, "text\r\n") == 0);
}

int main(void) {
    test_independent_sessions_and_line_endings();
    test_poll_dispatches_one_command();
    test_arguments();
    test_completion();
    test_overlong_line_is_truncated_at_documented_limit();
    test_interleaved_escape_sequences();
    test_rejected_inputs();
    test_excess_arguments_are_rejected();
    test_line_write_is_atomic();
    return 0;
}
