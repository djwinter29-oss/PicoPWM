#include "app/cli/pwm_commands.h"

#include "device.h"
#include "platform/boot_config.h"
#include "platform/system.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *input;
    size_t input_offset;
    char output[4096];
    size_t output_length;
    size_t output_limit;
} test_transport_t;

static pwm_role_t target_roles[PWM_BANK_COUNT] = {PWM_ROLE_MONITOR, PWM_ROLE_MONITOR, PWM_ROLE_MONITOR};
static pwm_role_t running_roles[PWM_BANK_COUNT] = {PWM_ROLE_MONITOR, PWM_ROLE_MONITOR, PWM_ROLE_MONITOR};
static boot_config_sysclk_t target_clock = BOOT_CONFIG_SYSCLK_DEFAULT;
static uint8_t target_address = 0x40u;
static uint8_t running_address = 0x40u;
static bool save_succeeds = true;
static bool reboot_scheduled;
static bool led_state;
static uint32_t set_calls;
static uint32_t restore_defaults_calls;
static pwm_driver_result_t restore_defaults_result = PWM_DRIVER_RESULT_OK;

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

    if (((transport->output_limit != 0u) && ((transport->output_length + length) > transport->output_limit)) ||
        ((transport->output_length + length) >= sizeof(transport->output))) {
        return false;
    }
    memcpy(transport->output + transport->output_length, data, length);
    transport->output_length += length;
    transport->output[transport->output_length] = '\0';
    return true;
}

bool device_get_channel(uint channel, pwm_state_t *state) {
    if ((channel >= device_channel_count()) || (state == NULL)) {
        return false;
    }
    *state = (pwm_state_t){1000u + channel, 50u};
    return true;
}

uint8_t device_channel_count(void) {
    return 24u;
}

pwm_driver_result_t device_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    (void)channel;
    (void)freq_hz;
    (void)duty;
    ++set_calls;
    return PWM_DRIVER_RESULT_OK;
}

pwm_driver_result_t device_restore_defaults(void) {
    ++restore_defaults_calls;
    return restore_defaults_result;
}

const char *pwm_driver_config_channel_name(uint channel) {
    (void)channel;
    return "HW";
}

bool pwm_driver_config_is_monitor(void) {
    return false;
}

uint32_t device_system_clock_hz(void) {
    return 125000000u;
}

uint8_t device_i2c_address(void) {
    return running_address;
}

pwm_role_t device_bank_role(pwm_bank_t bank) {
    return running_roles[bank];
}

const char *device_firmware_version(void) {
    return "test-version";
}

bool device_led_read(bool *on) {
    if (on == NULL) {
        return false;
    }
    *on = led_state;
    return true;
}

bool device_led_set(bool on) {
    led_state = on;
    return true;
}

void device_schedule_reboot(uint32_t delay_ms) {
    assert(delay_ms == 500u);
    reboot_scheduled = true;
}

bool system_clock_is_at_target(void) {
    return true;
}

pwm_role_t boot_config_get_bank(pwm_bank_t bank) {
    return target_roles[bank];
}

bool boot_config_set_bank(pwm_bank_t bank, pwm_role_t role) {
    if ((bank >= PWM_BANK_COUNT) || (role > PWM_ROLE_MONITOR)) {
        return false;
    }
    target_roles[bank] = role;
    return true;
}

uint8_t boot_config_get_i2c_address(void) {
    return target_address;
}

bool boot_config_set_i2c_address(uint8_t address) {
    if ((address < 8u) || (address > 119u)) {
        return false;
    }
    target_address = address;
    return true;
}

boot_config_sysclk_t boot_config_get_sysclk(void) {
    return target_clock;
}

bool boot_config_set_sysclk(boot_config_sysclk_t profile) {
    if ((unsigned)profile > BOOT_CONFIG_SYSCLK_250MHZ) {
        return false;
    }
    target_clock = profile;
    return true;
}

bool boot_config_save(void) {
    return save_succeeds;
}

static void reset_output(test_transport_t *transport, const char *input) {
    transport->input = input;
    transport->input_offset = 0u;
    transport->output_length = 0u;
    transport->output[0] = '\0';
}

static void run_command(pwm_commands_t *commands, test_transport_t *transport, const char *input) {
    reset_output(transport, input);
    pwm_commands_poll(commands);
}

static void test_grouped_commands(void) {
    test_transport_t transport = {0};
    shell_transport_t binding = {.read = test_read, .write = test_write, .context = &transport};
    pwm_commands_t commands;

    assert(pwm_commands_init(&commands, &binding));

    run_command(&commands, &transport, "pwm get 0\r");
    assert(strstr(transport.output, "CH0: freq=1000 Hz, duty=50%, enabled=yes") != NULL);

    run_command(&commands, &transport, "pwm get all\r");
    assert(strstr(transport.output, "All PWM channels") != NULL);
    assert(strstr(transport.output, "23  HW") != NULL);
    assert(transport.output_length <= 2048u);

    transport.output_limit = 150u;
    run_command(&commands, &transport, "pwm get all\r");
    assert(transport.output_length <= transport.output_limit);
    assert(strstr(transport.output, "ERR command handler failed") == NULL);
    transport.output_limit = 0u;
    assert(transport.output_length <= 2048u);

    set_calls = 0u;
    run_command(&commands, &transport, "pwm set 0 1000 25\r");
    assert(set_calls == 1u);
    assert(strstr(transport.output, "OK CH0 freq=1000 Hz duty=25%") != NULL);

    restore_defaults_calls = 0u;
    run_command(&commands, &transport, "pwm stop all\r");
    assert(restore_defaults_calls == 1u);
    assert(strstr(transport.output, "OK generator outputs stopped; monitor channels unchanged") != NULL);
    restore_defaults_result = PWM_DRIVER_RESULT_APPLY_FAILED;
    run_command(&commands, &transport, "pwm stop all\r");
    assert(strstr(transport.output, "ERR stop failed; some generator outputs may remain active") != NULL);
    restore_defaults_result = PWM_DRIVER_RESULT_OK;

    run_command(&commands, &transport, "board get clock\r");
    assert(strstr(transport.output, "clock=125000000 Hz target=met") != NULL);
    run_command(&commands, &transport, "board get version\r");
    assert(strstr(transport.output, "test-version") != NULL);
    run_command(&commands, &transport, "board get led\r");
    assert(strstr(transport.output, "led=off") != NULL);
    run_command(&commands, &transport, "board set led on\r");
    assert(led_state);
    assert(strstr(transport.output, "OK led on") != NULL);

    target_roles[0] = PWM_ROLE_MONITOR;
    target_clock = BOOT_CONFIG_SYSCLK_DEFAULT;
    target_address = 0x40u;
    save_succeeds = true;
    reboot_scheduled = false;
    run_command(&commands, &transport, "config set banka=generator, clock=150, address=64\r");
    assert(target_roles[0] == PWM_ROLE_GENERATOR);
    assert(target_clock == BOOT_CONFIG_SYSCLK_150MHZ);
    assert(target_address == 64u);
    assert(reboot_scheduled);
    assert(strstr(transport.output, "OK config saved; rebooting") != NULL);

    run_command(&commands, &transport, "config get banka\r");
    assert(strstr(transport.output, "banka running=monitor target=generator") != NULL);
    run_command(&commands, &transport, "config get\r");
    assert(strstr(transport.output, "clock running=125000000 Hz target=150 MHz") != NULL);

    pwm_role_t previous_role = target_roles[0];
    boot_config_sysclk_t previous_clock = target_clock;
    uint8_t previous_address = target_address;
    reboot_scheduled = false;
    run_command(&commands, &transport, "config set banka=monitor, clock=225\r");
    assert(target_roles[0] == previous_role);
    assert(target_clock == previous_clock);
    assert(target_address == previous_address);
    assert(!reboot_scheduled);
    assert(strstr(transport.output, "ERR invalid config assignment") != NULL);

    save_succeeds = false;
    run_command(&commands, &transport, "config set clock=200\r");
    assert(target_clock == previous_clock);
    assert(!reboot_scheduled);
    assert(strstr(transport.output, "ERR config save failed; settings unchanged") != NULL);
    save_succeeds = true;
}

static void test_help_commands(void) {
    test_transport_t transport = {0};
    shell_transport_t binding = {.read = test_read, .write = test_write, .context = &transport};
    pwm_commands_t commands;

    assert(pwm_commands_init(&commands, &binding));

    run_command(&commands, &transport, "?\r");
    assert(strstr(transport.output, "Groups: pwm, board, config") != NULL);

    run_command(&commands, &transport, "help pwm\r");
    assert(strstr(transport.output, "pwm get <channel|all>") != NULL);

    run_command(&commands, &transport, "config set ?\r");
    assert(strstr(transport.output, "banka/bankb/bankc=generator|monitor") != NULL);
    assert(strstr(transport.output, "clock=default|150|200|250") != NULL);
    assert(strstr(transport.output, "address=8..119") != NULL);

    run_command(&commands, &transport, "pwm get ?\r");
    assert(strstr(transport.output, "Usage: pwm get <channel|all>") != NULL);

    run_command(&commands, &transport, "board reboot ?\r");
    assert(strstr(transport.output, "Usage: board reboot") != NULL);
}

static void test_connection_output_retries(void) {
    static const char help[] = "Groups: pwm, board, config\r\n"
                               "Use 'help <group>' or '<group> ?' for command syntax.\r\n";
    test_transport_t transport = {0};
    shell_transport_t binding = {.read = test_read, .write = test_write, .context = &transport};
    pwm_commands_t commands;

    assert(!pwm_commands_init(NULL, &binding));
    assert(pwm_commands_init(&commands, &binding));

    transport.output_limit = sizeof(help) - 1u;
    assert(!pwm_commands_on_connected(&commands));
    assert(strcmp(transport.output, help) == 0);

    reset_output(&transport, "");
    transport.output_limit = 0u;
    assert(pwm_commands_on_connected(&commands));
    assert(strcmp(transport.output, "pico> ") == 0);

    assert(pwm_commands_on_disconnected(&commands));
    reset_output(&transport, "");
    assert(pwm_commands_on_connected(&commands));
    assert(strstr(transport.output, "Groups: pwm, board, config") != NULL);
    assert(strstr(transport.output, "pico> ") != NULL);
}

static void test_disconnect_resets_incomplete_command(void) {
    test_transport_t transport = {0};
    shell_transport_t binding = {.read = test_read, .write = test_write, .context = &transport};
    pwm_commands_t commands;

    led_state = false;
    assert(pwm_commands_init(&commands, &binding));

    run_command(&commands, &transport, "board set led ");
    assert(!led_state);

    assert(pwm_commands_on_disconnected(&commands));
    run_command(&commands, &transport, "on\r");
    assert(!led_state);
    assert(strstr(transport.output, "OK led on") == NULL);

    run_command(&commands, &transport, "board set led on\r");
    assert(led_state);
    assert(strstr(transport.output, "OK led on") != NULL);
}

int main(void) {
    test_grouped_commands();
    test_help_commands();
    test_connection_output_retries();
    test_disconnect_resets_incomplete_command();
    puts("cli_grouped_commands_test ok");
    return 0;
}
