/**
 * @file hid_protocol.c
 * @brief PicoPWM HID report layouts and the report 20 transaction, over `device`.
 */

#include "app/usb/hid_protocol.h"

#include "platform/boot_config.h"
#include "device.h"

#include <string.h>

/** @brief Layout version carried by every report. */
#define HID_LAYOUT_VERSION 1u
/** @brief HID `product_id` registered for PicoPWM. */
#define HID_PRODUCT_ID 3u
/** @brief Report 20 flag bit 0: the caller confirms a flash save. */
#define HID_FLAG_CONFIRM_SAVE 0x01u
/** @brief Banks reported by reports 10-12 and channels per bank. */
#define HID_BANK_COUNT 3u
#define HID_BANK_SIZE 8u
/** @brief Capability bit 0: the firmware can save its configuration. */
#define HID_CAP_SAVE 0x01u
/** @brief Capability bit 1: the firmware accepts SET_CLOCK_PROFILE. */
#define HID_CAP_CLOCK 0x02u
/** @brief Capability bit 2: confirmed save followed by reboot is available. */
#define HID_CAP_SAVE_REBOOT 0x04u
/** @brief Capability bit 3: identification LED control and readback. */
#define HID_CAP_LED 0x08u
/** @brief Capability bit 4: confirmed reboot without saving. */
#define HID_CAP_REBOOT 0x10u
/** @brief Capability bit 5: firmware memory report is available. */
#define HID_CAP_MEMORY 0x20u
/** @brief Capability bit 6: runtime telemetry report is available. */
#define HID_CAP_RUNTIME 0x40u
/** @brief Accepted clock profiles, one bit per @ref boot_config_sysclk_t value (default, 150, 200, 250 MHz). */
#define HID_CLOCK_PROFILE_MASK 0x0Fu
/** @brief Report 20 flag bit 1: the caller acknowledges an overclock profile (above 150 MHz). */
#define HID_FLAG_ACK_OVERCLOCK 0x02u

/** @brief Single pending-request slot and the latest device-global result. */
static struct {
    uint8_t request[HID_ACTION_SIZE]; /**< Request bytes copied by the SET callback. */
    bool busy;                        /**< Set from acceptance until the final result is published. */
    uint8_t transaction_id;           /**< Echo of the latest accepted request. */
    uint8_t operation;                /**< Raw operation byte of the latest accepted request. */
    uint8_t status;                   /**< hid_status_t of the latest accepted request. */
    bool reboot_scheduled;            /**< Reject new requests until the scheduled reboot. */
} action;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

/** @brief Parse "major.minor.patch" prefixes; missing or oversized parts saturate to 0 and 255. */
static void parse_version(const char *text, uint8_t out[3]) {
    for (uint i = 0; i < 3u; ++i) {
        uint32_t value = 0u;
        while (*text >= '0' && *text <= '9') {
            value = value * 10u + (uint32_t)(*text++ - '0');
            if (value > 255u) {
                value = 255u;
            }
        }
        out[i] = (uint8_t)value;
        if (*text == '.') {
            ++text;
        }
    }
}

static void pack_bank_config(uint8_t *out, bool running) {
    /* Bytes 0-2 are the fixed backend ids (A=HW 0, B=PIO 1, C=SW 2), kept for wire compatibility. */
    out[0] = 0u;
    out[1] = 1u;
    out[2] = 2u;
    for (uint8_t bank = 0u; bank < PWM_BANK_COUNT; ++bank) {
        out[3 + bank] =
            (uint8_t)(running ? device_bank_role((pwm_bank_t)bank) : boot_config_get_bank((pwm_bank_t)bank));
    }
    out[6] = running ? device_i2c_address() : boot_config_get_i2c_address();
}

static bool build_identity(uint8_t *payload) {
    bool led_on;
    memset(payload, 0, HID_IDENTITY_SIZE);
    payload[0] = HID_LAYOUT_VERSION;
    payload[1] = HID_PRODUCT_ID;
    device_get_uid(&payload[3]);
    parse_version(device_firmware_version(), &payload[11]);
#ifdef PICO_RP2350
    payload[14] = 2u;
#else
    payload[14] = 1u;
#endif
    payload[15] = device_channel_count();
    payload[16] = HID_CAP_SAVE | HID_CAP_CLOCK | HID_CAP_SAVE_REBOOT | HID_CAP_REBOOT | HID_CAP_MEMORY | HID_CAP_RUNTIME;
    if (device_led_read(&led_on)) {
        payload[16] |= HID_CAP_LED;
    }
    payload[17] = HID_CLOCK_PROFILE_MASK;
    return true;
}

static bool build_configuration(uint8_t *payload) {
    bool led_on;
    memset(payload, 0, HID_CONFIGURATION_SIZE);
    payload[0] = HID_LAYOUT_VERSION;
    pack_bank_config(&payload[1], true);
    pack_bank_config(&payload[8], false);
    if (device_led_read(&led_on)) {
        payload[15] = led_on ? 1u : 0u;
    }
    payload[16] = (uint8_t)boot_config_get_sysclk();
    put_le32(&payload[17], device_system_clock_hz());
    return true;
}

static bool build_bank_status(uint8_t bank, uint8_t *payload) {
    payload[0] = HID_LAYOUT_VERSION;
    for (uint i = 0; i < HID_BANK_SIZE; ++i) {
        pwm_state_t state;

        if (!device_get_channel(bank * HID_BANK_SIZE + i, &state)) {
            return false;
        }
        put_le32(&payload[1 + i * 5u], state.freq_hz);
        payload[5 + i * 5u] = state.duty;
    }
    return true;
}

uint16_t hid_protocol_get(uint8_t report_id, uint8_t *payload, uint16_t capacity) {
    uint8_t scratch[HID_MAX_PAYLOAD_SIZE];
    uint16_t length;
    bool ok;

    if (payload == NULL) {
        return 0u;
    }
    if (report_id == HID_REPORT_ID_IDENTITY) {
        length = HID_IDENTITY_SIZE;
        ok = build_identity(scratch);
    } else if (report_id == HID_REPORT_ID_CONFIGURATION) {
        length = HID_CONFIGURATION_SIZE;
        ok = build_configuration(scratch);
    } else if (report_id == HID_REPORT_ID_MEMORY) {
        length = HID_MEMORY_SIZE;
        scratch[0] = HID_LAYOUT_VERSION;
        put_le32(&scratch[1], device_ram_reserved_bytes());
        put_le32(&scratch[5], device_ram_total_bytes());
        ok = true;
    } else if (report_id == HID_REPORT_ID_RUNTIME) {
        length = HID_RUNTIME_SIZE;
        scratch[0] = HID_LAYOUT_VERSION;
        put_le32(&scratch[1], device_uptime_seconds());
        put_le32(&scratch[5], (uint32_t)device_temperature_millic());
        uint16_t core0 = device_service_load(false);
        uint16_t core1 = device_service_load(true);
        scratch[9] = (uint8_t)core0;
        scratch[10] = (uint8_t)(core0 >> 8);
        scratch[11] = (uint8_t)core1;
        scratch[12] = (uint8_t)(core1 >> 8);
        ok = true;
    } else if (report_id >= HID_REPORT_ID_BANK_STATUS && report_id < HID_REPORT_ID_BANK_STATUS + HID_BANK_COUNT) {
        length = HID_BANK_STATUS_SIZE;
        ok = build_bank_status((uint8_t)(report_id - HID_REPORT_ID_BANK_STATUS), scratch);
    } else if (report_id == HID_REPORT_ID_ACTION) {
        length = HID_ACTION_SIZE;
        memset(scratch, 0, length);
        scratch[0] = HID_LAYOUT_VERSION;
        scratch[1] = action.transaction_id;
        scratch[2] = action.operation;
        scratch[3] = action.status;
        ok = true;
    } else {
        return 0u;
    }
    if (!ok || capacity < length) {
        return 0u;
    }
    memcpy(payload, scratch, length);
    if (report_id == HID_REPORT_ID_ACTION &&
        (action.operation == HID_OP_SAVE_REBOOT || action.operation == HID_OP_REBOOT) &&
        action.status == HID_STATUS_OK && !action.reboot_scheduled) {
        action.reboot_scheduled = true;
        device_schedule_reboot(500u);
    }
    return length;
}

bool hid_protocol_submit(const uint8_t *payload, uint16_t length) {
    if (payload == NULL || length != HID_ACTION_SIZE || action.busy || action.reboot_scheduled ||
        ((action.operation == HID_OP_SAVE_REBOOT || action.operation == HID_OP_REBOOT) &&
         action.status == HID_STATUS_OK)) {
        return false;
    }
    memcpy(action.request, payload, HID_ACTION_SIZE);
    action.transaction_id = payload[1];
    action.operation = payload[2];
    action.status = HID_STATUS_PENDING;
    action.busy = true;
    return true;
}

static bool reserved_clear(const uint8_t *r) {
    return r[10] == 0u && r[11] == 0u;
}

/** @brief Acknowledgement flag a save must carry while the target clock is an overclock. */
static uint8_t overclock_flag(void) {
    return boot_config_get_sysclk() > BOOT_CONFIG_SYSCLK_150MHZ ? HID_FLAG_ACK_OVERCLOCK : 0u;
}

static hid_status_t execute(const uint8_t *r) {
    uint8_t operation = r[2];
    uint8_t channel = r[3];
    uint32_t arg0 = le32(&r[4]);
    uint8_t arg1 = r[8];
    uint8_t flags = r[9];

    if (r[0] != HID_LAYOUT_VERSION || operation < HID_OP_SET_CHANNEL || operation > HID_OP_REBOOT) {
        return HID_STATUS_UNSUPPORTED;
    }
    if (r[1] == 0u || !reserved_clear(r)) {
        return HID_STATUS_INVALID;
    }

    switch (operation) {
    case HID_OP_SET_CHANNEL: {
        uint8_t bank = (uint8_t)(channel / HID_BANK_SIZE);

        if (channel >= device_channel_count() || arg1 > 100u || flags != 0u) {
            return HID_STATUS_INVALID;
        }
        if (device_bank_role((pwm_bank_t)bank) != PWM_ROLE_GENERATOR) {
            return HID_STATUS_INVALID;
        }
        return (hid_status_t)device_set_channel(channel, arg0, arg1);
    }
    case HID_OP_STOP:
        if (channel != 0u || arg0 != 0u || arg1 != 0u || flags != 0u) {
            return HID_STATUS_INVALID;
        }
        return (hid_status_t)device_restore_defaults();
    case HID_OP_SET_BANK:
        if (channel >= HID_BANK_COUNT || (arg0 & 0xFFFF0000u) != 0u || arg1 != 0u || flags != 0u ||
            (arg0 & 0xFFu) != channel ||
            !boot_config_set_bank((pwm_bank_t)channel, (pwm_role_t)((arg0 >> 8) & 0xFFu))) {
            return HID_STATUS_INVALID;
        }
        return HID_STATUS_OK;
    case HID_OP_SET_I2C_ADDRESS:
        if (channel != 0u || arg0 > 0xFFu || arg1 != 0u || flags != 0u || !boot_config_set_i2c_address((uint8_t)arg0)) {
            return HID_STATUS_INVALID;
        }
        return HID_STATUS_OK;
    case HID_OP_SAVE_CONFIG:
    case HID_OP_SAVE_REBOOT:
        if (channel != 0u || arg0 != 0u || arg1 != 0u || flags != (HID_FLAG_CONFIRM_SAVE | overclock_flag())) {
            return HID_STATUS_INVALID;
        }
        return boot_config_save() ? HID_STATUS_OK : HID_STATUS_STORAGE_FAILED;
    case HID_OP_SET_LED:
        if (channel != 0u || arg0 > 1u || arg1 != 0u || flags != 0u) {
            return HID_STATUS_INVALID;
        }
        return device_led_set(arg0 != 0u) ? HID_STATUS_OK : HID_STATUS_UNAVAILABLE;
    case HID_OP_REBOOT:
        if (channel != 0u || arg0 != 0u || arg1 != 0u || flags != HID_FLAG_CONFIRM_SAVE) {
            return HID_STATUS_INVALID;
        }
        return HID_STATUS_OK;
    default: // HID_OP_SET_CLOCK_PROFILE
        if (channel != 0u || arg0 > BOOT_CONFIG_SYSCLK_250MHZ || arg1 != 0u ||
            flags != ((arg0 > BOOT_CONFIG_SYSCLK_150MHZ) ? HID_FLAG_ACK_OVERCLOCK : 0u) ||
            !boot_config_set_sysclk((boot_config_sysclk_t)arg0)) {
            return HID_STATUS_INVALID;
        }
        return HID_STATUS_OK;
    }
}

void hid_protocol_poll(void) {
    uint8_t request[HID_ACTION_SIZE];

    if (!action.busy) {
        return;
    }
    memcpy(request, action.request, sizeof(request));
    action.status = (uint8_t)execute(request);
    action.busy = false;
}

void hid_protocol_reset(void) {
    memset(&action, 0, sizeof(action));
}
