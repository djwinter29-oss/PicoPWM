#include "cli/shell.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const char *input;
    size_t input_offset;
    char output[512];
    size_t output_length;
} test_transport_t;

typedef struct {
    shell_t *shell;
    uint32_t command_calls;
    uint32_t completion_calls;
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

    assert(transport->output_length + length < sizeof(transport->output));
    memcpy(transport->output + transport->output_length, data, length);
    transport->output_length += length;
    transport->output[transport->output_length] = '\0';
    return true;
}

static bool test_command(void *context, int argc, const char *const *argv) {
    test_context_t *test = context;

    assert(argc == 1);
    assert(strcmp(argv[0], "help") == 0);
    test->command_calls++;
    return shell_write_line(test->shell, "handled");
}

static char **test_complete(void *context, int argc, const char *const *argv) {
    static char *matches[] = {(char *)"help", NULL};
    test_context_t *test = context;

    assert(argc == 1);
    assert(strcmp(argv[0], "he") == 0);
    test->completion_calls++;
    return matches;
}

static shell_config_t test_config(test_context_t *context, const shell_transport_t *transport) {
    static const shell_command_t commands[] = {
        {"help", "help", test_command},
    };

    return (shell_config_t){
        .transport = transport,
        .commands = commands,
        .command_count = 1u,
        .unknown_message = "unknown",
        .unknown_handler = NULL,
        .completion_handler = test_complete,
        .command_context = context,
    };
}

static void test_independent_sessions_and_line_endings(void) {
    test_transport_t transport_a = {.input = "help\r"};
    test_transport_t transport_b = {.input = "help\n"};
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

static void test_completion(void) {
    test_transport_t transport = {.input = "he\t\r"};
    shell_t shell;
    test_context_t context = {.shell = &shell};
    shell_transport_t binding = {.read = test_read, .write = test_write, .context = &transport};
    shell_config_t config = test_config(&context, &binding);

    shell_init(&shell, &config);
    shell_poll(&shell);

    assert(context.completion_calls == 1u);
    assert(context.command_calls == 1u);
}

static void test_interleaved_escape_sequences(void) {
    test_transport_t transport_a = {.input = "\033"};
    test_transport_t transport_b = {.input = "help\r"};
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

    transport_a.input = "[Dhelp\r";
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
    shell_init(&shell, &config);
    assert(!shell.initialized);

    config = test_config(&context, &binding);
    config.commands = NULL;
    shell_init(&shell, &config);
    assert(!shell.initialized);

    config = test_config(&context, &binding);
    config.commands = nameless;
    shell_init(&shell, &config);
    assert(!shell.initialized);

    config = test_config(&context, &binding);
    shell_init(&shell, &config);
    shell_poll(&shell);
    assert(strstr(transport.output, "unknown") != NULL);
    assert(shell_write_line(&shell, NULL));

    config.unknown_handler = test_unknown_handled;
    config.completion_handler = NULL;
    transport.input = "nope\r";
    transport.input_offset = 0u;
    transport.output_length = 0u;
    transport.output[0] = '\0';
    shell_init(&shell, &config);
    shell_prompt(&shell);
    shell_poll(&shell);
    assert(strstr(transport.output, "unknown") == NULL);

    config = test_config(&context, &binding);
    config.commands = failing;
    transport.input = "fail\r";
    transport.input_offset = 0u;
    shell_init(&shell, &config);
    shell_poll(&shell);

    config.commands = reenter;
    transport.input = "re\r";
    transport.input_offset = 0u;
    shell_init(&shell, &config);
    shell_poll(&shell);
}

int main(void) {
    test_independent_sessions_and_line_endings();
    test_completion();
    test_interleaved_escape_sequences();
    test_rejected_inputs();
    return 0;
}
