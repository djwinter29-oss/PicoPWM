#include "app/usb/hid_protocol.h"

#include "platform/boot_config.h"
#include "control_api.h"

#include <assert.h>
#include <string.h>

static bool save_ok = true;
static int saves;
static int channel_sets;
static uint last_channel;
static uint32_t last_freq;
static uint8_t last_duty;
static pwm_role_t bank_role;
static uint32_t reboot_delay;
static int reboots;
static bool led_on;
static bool led_available = true;

void control_api_get_pin_assignment(control_api_pin_assignment_t *pins) {
    for (uint channel = 0; channel < PWM_DRIVER_CHANNEL_COUNT; ++channel) {
        pins->channels[channel] = (uint8_t)channel;
    }
    pins->i2c_sda = 26u;
    pins->i2c_scl = 27u;
    pins->led = led_available ? 25u : UINT8_MAX;
}

uint32_t control_api_ram_reserved_bytes(void) {
    return 32768u;
}

uint32_t control_api_ram_total_bytes(void) {
    return 270336u;
}

uint32_t control_api_uptime_seconds(void) {
    return 123u;
}

int32_t control_api_temperature_millic(void) {
    return -5000;
}

uint16_t control_api_service_load(bool core1) {
    return core1 ? 2500u : 100u;
}

bool control_api_led_read(bool *on) {
    *on = led_on;
    return led_available;
}

bool control_api_led_set(bool on) {
    if (!led_available) {
        return false;
    }
    led_on = on;
    return true;
}

void control_api_schedule_reboot(uint32_t delay_ms) {
    reboot_delay = delay_ms;
    ++reboots;
}

const char *control_api_firmware_version(void) {
    return "1.2.3-dev";
}

pwm_role_t boot_config_get_bank(pwm_bank_t bank) {
    return bank == PWM_BANK_B ? PWM_ROLE_MONITOR : PWM_ROLE_GENERATOR;
}

uint8_t boot_config_get_i2c_address(void) {
    return 0x20u;
}

pwm_role_t control_api_bank_role(pwm_bank_t bank) {
    return boot_config_get_bank(bank);
}

uint8_t control_api_i2c_address(void) {
    return 0x20u;
}

bool boot_config_set_bank(pwm_bank_t bank, pwm_role_t role) {
    (void)bank;
    bank_role = role;
    return true;
}

static boot_config_sysclk_t sysclk;

boot_config_sysclk_t boot_config_get_sysclk(void) {
    return sysclk;
}

bool boot_config_set_sysclk(boot_config_sysclk_t profile) {
    sysclk = profile;
    return true;
}

bool boot_config_save(void) {
    ++saves;
    return save_ok;
}

bool boot_config_set_i2c_address(uint8_t address) {
    return address != 0u;
}

void control_api_get_uid(uint8_t uid[8]) {
    for (uint8_t i = 0; i < 8u; ++i) {
        uid[i] = (uint8_t)(0xA0u + i);
    }
}

uint32_t control_api_system_clock_hz(void) {
    return 0x08F0D180u;
}

uint8_t control_api_channel_count(void) {
    return PWM_DRIVER_CHANNEL_COUNT;
}

bool control_api_get_channel(uint channel, pwm_state_t *state) {
    *state = (pwm_state_t){1000u + channel, (uint8_t)channel};
    return true;
}

pwm_driver_result_t control_api_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    ++channel_sets;
    last_channel = channel;
    last_freq = freq_hz;
    last_duty = duty;
    return PWM_DRIVER_RESULT_OK;
}

pwm_driver_result_t control_api_restore_defaults(void) {
    return PWM_DRIVER_RESULT_OK;
}

static void send(uint8_t id, uint8_t op, uint8_t channel, uint32_t arg0, uint8_t arg1, uint8_t flags) {
    uint8_t r[HID_ACTION_SIZE] = {
        1,    id,    op, channel, (uint8_t)arg0, (uint8_t)(arg0 >> 8), (uint8_t)(arg0 >> 16), (uint8_t)(arg0 >> 24),
        arg1, flags, 0,  0};
    assert(hid_protocol_submit(r, sizeof(r)));
    hid_protocol_poll();
}

static uint8_t result_status(uint8_t id, uint8_t op) {
    uint8_t p[HID_ACTION_SIZE];
    assert(hid_protocol_get(HID_REPORT_ID_ACTION, p, sizeof(p)) == HID_ACTION_SIZE);
    assert(p[0] == 1u && p[1] == id && p[2] == op);
    return p[3];
}

int main(void) {
    uint8_t p[HID_MAX_PAYLOAD_SIZE];
    static const uint8_t identity[] = {1,    3,    0, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5,
                                       0xA6, 0xA7, 1, 2,    3,    1,    24,   255,  0x0F};
    static const uint8_t config[] = {1, 0, 1, 2, 0, 1, 0, 0x20, 0, 1, 2, 0, 1, 0, 0x20, 1, 0, 0x80, 0xD1, 0xF0, 0x08};

    assert(hid_protocol_get(HID_REPORT_ID_IDENTITY, p, sizeof(p)) == sizeof(identity));
    assert(memcmp(p, identity, sizeof(identity)) == 0);
    static const uint8_t memory[] = {1, 0, 0x80, 0, 0, 0, 0x20, 4, 0};
    assert(hid_protocol_get(HID_REPORT_ID_MEMORY, p, sizeof(p)) == sizeof(memory));
    assert(memcmp(p, memory, sizeof(memory)) == 0);
    assert(hid_protocol_get(HID_REPORT_ID_MEMORY, p, sizeof(memory) - 1) == 0u);
    static const uint8_t runtime[] = {1, 123, 0, 0, 0, 0x78, 0xEC, 0xFF, 0xFF, 100, 0, 0xC4, 9};
    assert(hid_protocol_get(HID_REPORT_ID_RUNTIME, p, sizeof(p)) == sizeof(runtime));
    assert(memcmp(p, runtime, sizeof(runtime)) == 0);
    assert(hid_protocol_get(HID_REPORT_ID_RUNTIME, p, sizeof(runtime) - 1) == 0u);
    assert(hid_protocol_get(HID_REPORT_ID_PINS, p, sizeof(p)) == HID_PINS_SIZE);
    assert(p[0] == 1u && p[25] == 26u && p[26] == 27u && p[27] == 25u);
    for (uint channel = 0; channel < PWM_DRIVER_CHANNEL_COUNT; ++channel) {
        assert(p[channel + 1] == channel);
    }
    assert(hid_protocol_get(HID_REPORT_ID_PINS, p, HID_PINS_SIZE - 1) == 0u);
    assert(hid_protocol_get(HID_REPORT_ID_IDENTITY, p, 17) == 0u);
    assert(hid_protocol_get(HID_REPORT_ID_CONFIGURATION, p, sizeof(p)) == sizeof(config));
    assert(p[0] == 1u && p[15] == 0u && memcmp(&p[1], &config[1], 7) == 0 && memcmp(&p[17], &config[17], 4) == 0);
    assert(hid_protocol_get(HID_REPORT_ID_BANK_STATUS + 2, p, sizeof(p)) == HID_BANK_STATUS_SIZE);
    assert(p[0] == 1u && p[1] == (1016u & 0xFFu) && p[2] == (1016u >> 8) && p[5] == 16u);
    assert(hid_protocol_get(13u, p, sizeof(p)) == 0u);

    assert(result_status(0, 0) == 0u);
    hid_protocol_reset();

    send(7, HID_OP_SET_CHANNEL, 3, 1500u, 25, 0);
    assert(channel_sets == 1 && last_channel == 3u && last_freq == 1500u && last_duty == 25u);
    assert(result_status(7, HID_OP_SET_CHANNEL) == HID_STATUS_OK);

    send(8, HID_OP_SET_CHANNEL, 8, 1500u, 25, 0);
    assert(result_status(8, HID_OP_SET_CHANNEL) == HID_STATUS_INVALID && channel_sets == 1);
    send(9, HID_OP_SET_CHANNEL, 0, 1500u, 101, 0);
    assert(result_status(9, HID_OP_SET_CHANNEL) == HID_STATUS_INVALID);
    send(0, HID_OP_STOP, 0, 0, 0, 0);
    assert(result_status(0, HID_OP_STOP) == HID_STATUS_INVALID);
    send(10, 99, 0, 0, 0, 0);
    assert(result_status(10, 99) == HID_STATUS_UNSUPPORTED);
    send(11, HID_OP_STOP, 0, 0, 0, 0);
    assert(result_status(11, HID_OP_STOP) == HID_STATUS_OK);

    send(12, HID_OP_SET_BANK, 1, 1u | (1u << 8), 0, 0);
    assert(result_status(12, HID_OP_SET_BANK) == HID_STATUS_OK && bank_role == 1);
    send(40, HID_OP_SET_BANK, 1, 2u, 0, 0);
    assert(result_status(40, HID_OP_SET_BANK) == HID_STATUS_INVALID);
    send(13, HID_OP_SET_I2C_ADDRESS, 0, 0x21, 0, 0);
    assert(result_status(13, HID_OP_SET_I2C_ADDRESS) == HID_STATUS_OK);
    send(14, HID_OP_SET_I2C_ADDRESS, 0, 0x100, 0, 0);
    assert(result_status(14, HID_OP_SET_I2C_ADDRESS) == HID_STATUS_INVALID);

    send(15, HID_OP_SAVE_CONFIG, 0, 0, 0, 0);
    assert(result_status(15, HID_OP_SAVE_CONFIG) == HID_STATUS_INVALID && saves == 0);
    send(16, HID_OP_SAVE_CONFIG, 0, 0, 0, 1);
    assert(result_status(16, HID_OP_SAVE_CONFIG) == HID_STATUS_OK && saves == 1);
    save_ok = false;
    send(17, HID_OP_SAVE_CONFIG, 0, 0, 0, 1);
    assert(result_status(17, HID_OP_SAVE_CONFIG) == HID_STATUS_STORAGE_FAILED);

    // Profiles above 150 MHz need the overclock flag, and a save then needs it too.
    send(18, HID_OP_SET_CLOCK_PROFILE, 0, 1, 0, 0);
    assert(result_status(18, HID_OP_SET_CLOCK_PROFILE) == HID_STATUS_OK && sysclk == BOOT_CONFIG_SYSCLK_150MHZ);
    send(18, HID_OP_SET_CLOCK_PROFILE, 0, 3, 0, 0);
    assert(result_status(18, HID_OP_SET_CLOCK_PROFILE) == HID_STATUS_INVALID && sysclk == BOOT_CONFIG_SYSCLK_150MHZ);
    send(18, HID_OP_SET_CLOCK_PROFILE, 0, 4, 0, 2);
    assert(result_status(18, HID_OP_SET_CLOCK_PROFILE) == HID_STATUS_INVALID);
    send(18, HID_OP_SET_CLOCK_PROFILE, 0, 3, 0, 2);
    assert(result_status(18, HID_OP_SET_CLOCK_PROFILE) == HID_STATUS_OK && sysclk == BOOT_CONFIG_SYSCLK_250MHZ);
    assert(hid_protocol_get(HID_REPORT_ID_CONFIGURATION, p, sizeof(p)) == sizeof(config) && p[16] == 3u);
    save_ok = true;
    send(19, HID_OP_SAVE_CONFIG, 0, 0, 0, 1);
    assert(result_status(19, HID_OP_SAVE_CONFIG) == HID_STATUS_INVALID && saves == 2);
    send(19, HID_OP_SAVE_CONFIG, 0, 0, 0, 3);
    assert(result_status(19, HID_OP_SAVE_CONFIG) == HID_STATUS_OK && saves == 3);

    // A request is PENDING until polled, and a second SET is dropped meanwhile.
    uint8_t r[HID_ACTION_SIZE] = {1, 20, HID_OP_STOP};
    assert(hid_protocol_submit(r, sizeof(r)));
    assert(result_status(20, HID_OP_STOP) == HID_STATUS_PENDING);
    r[1] = 21;
    assert(!hid_protocol_submit(r, sizeof(r)));
    assert(!hid_protocol_submit(r, sizeof(r) - 1u));
    hid_protocol_poll();
    assert(result_status(20, HID_OP_STOP) == HID_STATUS_OK);

    hid_protocol_reset();
    assert(result_status(0, 0) == 0u);

    send(22, HID_OP_SAVE_REBOOT, 0, 0, 0, 1);
    assert(result_status(22, HID_OP_SAVE_REBOOT) == HID_STATUS_INVALID && reboots == 0);
    save_ok = false;
    send(23, HID_OP_SAVE_REBOOT, 0, 0, 0, 3);
    assert(result_status(23, HID_OP_SAVE_REBOOT) == HID_STATUS_STORAGE_FAILED && reboots == 0);
    save_ok = true;
    send(24, HID_OP_SAVE_REBOOT, 0, 0, 0, 3);
    assert(reboots == 0);
    assert(hid_protocol_get(HID_REPORT_ID_ACTION, p, 1) == 0u && reboots == 0);
    assert(result_status(24, HID_OP_SAVE_REBOOT) == HID_STATUS_OK && reboots == 1 && reboot_delay == 500u);
    assert(result_status(24, HID_OP_SAVE_REBOOT) == HID_STATUS_OK && reboots == 1);
    assert(!hid_protocol_submit(r, sizeof(r)));
    hid_protocol_reset();
    send(25, HID_OP_SET_LED, 0, 1, 0, 0);
    assert(result_status(25, HID_OP_SET_LED) == HID_STATUS_OK && led_on);
    assert(hid_protocol_get(HID_REPORT_ID_CONFIGURATION, p, sizeof(p)) == HID_CONFIGURATION_SIZE && p[15] == 1u);
    send(26, HID_OP_SET_LED, 0, 2, 0, 0);
    assert(result_status(26, HID_OP_SET_LED) == HID_STATUS_INVALID && led_on);
    send(27, HID_OP_SET_LED, 0, 0, 0, 0);
    assert(result_status(27, HID_OP_SET_LED) == HID_STATUS_OK && !led_on);
    led_available = false;
    send(28, HID_OP_SET_LED, 0, 1, 0, 0);
    assert(result_status(28, HID_OP_SET_LED) == HID_STATUS_UNAVAILABLE);
    assert(hid_protocol_get(HID_REPORT_ID_IDENTITY, p, sizeof(p)) == HID_IDENTITY_SIZE && p[16] == 247u);
    assert(hid_protocol_get(HID_REPORT_ID_PINS, p, sizeof(p)) == HID_PINS_SIZE && p[27] == UINT8_MAX);
    int saved_before_reboot = saves;
    send(29, HID_OP_REBOOT, 0, 0, 0, 0);
    assert(result_status(29, HID_OP_REBOOT) == HID_STATUS_INVALID && reboots == 1);
    send(30, HID_OP_REBOOT, 0, 0, 0, 1);
    assert(reboots == 1);
    assert(result_status(30, HID_OP_REBOOT) == HID_STATUS_OK && reboots == 2 && saves == saved_before_reboot);
    return 0;
}
