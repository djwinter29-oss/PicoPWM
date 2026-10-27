#include "app/usb/hid_protocol.h"

#include "app/device_service/device_service.h"

#include <assert.h>
#include <string.h>

static bool save_ok = true;
static bool dirty = true;
static int saves;
static int channel_sets;
static uint last_channel;
static uint32_t last_freq;
static uint8_t last_duty;
static pwm_driver_config_bank_backend_t bank_backend;
static pwm_driver_config_bank_role_t bank_role;
static pwm_driver_config_t running_config = {
    .bank_a_backend = PWM_DRIVER_CONFIG_BANK_BACKEND_HW,
    .bank_b_backend = PWM_DRIVER_CONFIG_BANK_BACKEND_PIO,
    .bank_c_backend = PWM_DRIVER_CONFIG_BANK_BACKEND_SW,
    .bank_a_role = PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR,
    .bank_b_role = PWM_DRIVER_CONFIG_BANK_ROLE_MONITOR,
    .bank_c_role = PWM_DRIVER_CONFIG_BANK_ROLE_GENERATOR,
    .i2c_address = 0x20,
};

const char *device_service_firmware_version(void) {
    return "1.2.3-dev";
}

bool device_service_config_get_running(pwm_driver_config_t *config) {
    *config = running_config;
    return true;
}

bool device_service_config_get_target(pwm_driver_config_t *config) {
    *config = running_config;
    return true;
}

bool device_service_config_set_bank(pwm_driver_config_bank_t bank, pwm_driver_config_bank_backend_t backend,
                                pwm_driver_config_bank_role_t role) {
    (void)bank;
    bank_backend = backend;
    bank_role = role;
    return backend <= PWM_DRIVER_CONFIG_BANK_BACKEND_SW;
}

bool device_service_config_save_target(void) {
    ++saves;
    return save_ok;
}

bool device_service_config_target_dirty(void) {
    return dirty;
}

bool device_service_config_set_i2c_address(uint8_t address) {
    return address != 0u;
}

void device_service_get_uid(uint8_t uid[8]) {
    for (uint8_t i = 0; i < 8u; ++i) {
        uid[i] = (uint8_t)(0xA0u + i);
    }
}

uint32_t device_service_system_clock_hz(void) {
    return 0x08F0D180u;
}

uint8_t device_service_channel_count(void) {
    return PWM_DRIVER_CHANNEL_COUNT;
}

bool device_service_get_channel(uint channel, pwm_driver_state_t *state) {
    *state = (pwm_driver_state_t){1000u + channel, (uint8_t)channel, 0u};
    return true;
}

pwm_driver_result_t device_service_set_channel(uint channel, uint32_t freq_hz, uint8_t duty) {
    ++channel_sets;
    last_channel = channel;
    last_freq = freq_hz;
    last_duty = duty;
    return PWM_DRIVER_RESULT_OK;
}

pwm_driver_result_t device_service_restore_defaults(void) {
    return PWM_DRIVER_RESULT_OK;
}

static void send(uint8_t id, uint8_t op, uint8_t channel, uint32_t arg0, uint8_t arg1, uint8_t flags) {
    uint8_t r[HID_ACTION_SIZE] = {1, id, op, channel, (uint8_t)arg0, (uint8_t)(arg0 >> 8), (uint8_t)(arg0 >> 16),
                                  (uint8_t)(arg0 >> 24), arg1, flags, 0, 0};
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
    static const uint8_t identity[] = {1, 3, 0, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
                                       1, 2, 3, 1, 24, 1, 0};
    static const uint8_t config[] = {1, 0, 1, 2, 0, 1, 0, 0x20, 0, 1, 2, 0, 1, 0, 0x20, 1, 0, 0x80, 0xD1, 0xF0, 0x08};

    assert(hid_protocol_get(HID_REPORT_ID_IDENTITY, p, sizeof(p)) == sizeof(identity));
    assert(memcmp(p, identity, sizeof(identity)) == 0);
    assert(hid_protocol_get(HID_REPORT_ID_IDENTITY, p, 17) == 0u);
    assert(hid_protocol_get(HID_REPORT_ID_CONFIGURATION, p, sizeof(p)) == sizeof(config));
    assert(p[0] == 1u && p[15] == 1u && memcmp(&p[1], &config[1], 7) == 0 && memcmp(&p[17], &config[17], 4) == 0);
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
    assert(result_status(12, HID_OP_SET_BANK) == HID_STATUS_OK && bank_backend == 1 && bank_role == 1);
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
    send(18, HID_OP_SET_CLOCK_PROFILE, 0, 0, 0, 0);
    assert(result_status(18, HID_OP_SET_CLOCK_PROFILE) == HID_STATUS_INVALID);

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
    return 0;
}
