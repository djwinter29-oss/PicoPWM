#include "i2c_slave.h"
#include "pwmdriver/pwm_driver.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include <string.h>

// I2C0 slave on GPIO 16 (SDA) / 17 (SCL).
#define I2C_SLAVE_INST    i2c0
#define I2C_SLAVE_ADDR    0x40
#define I2C_SDA_PIN       16
#define I2C_SCL_PIN       17

#define RESP_BUF_SIZE     64

static uint8_t resp_buf[RESP_BUF_SIZE];
static uint8_t resp_len = 0;
static uint8_t resp_idx = 0;

static void prepare_response(uint8_t cmd) {
    resp_idx = 0;
    if (cmd == 0x00) {
        // Device type string, null-terminated.
        const char *s = "PicoPWM";
        resp_len = (uint8_t)(strlen(s) + 1);
        memcpy(resp_buf, s, resp_len);
    } else if (cmd == 0x01) {
        // Version string, null-terminated.
        const char *s = "1.0.0";
        resp_len = (uint8_t)(strlen(s) + 1);
        memcpy(resp_buf, s, resp_len);
    } else if (cmd == 0x02) {
        // Channel count (1 byte).
        resp_buf[0] = PWM_DRIVER_CHANNEL_COUNT;
        resp_len = 1;
    } else if (cmd >= 0x10 && cmd <= 0x27) {
        // Channel properties: freq(float), duty(float), pulse_count(uint32).
        // All values are little-endian, 12 bytes total.
        int ch = cmd - 0x10;
        pwm_driver_state_t state = {0.0f, 0.5f, 0};
        control_get(ch, &state);
        memcpy(resp_buf + 0, &state.freq_hz, sizeof(float));
        memcpy(resp_buf + 4, &state.duty, sizeof(float));
        memcpy(resp_buf + 8, &state.pulse_count, sizeof(uint32_t));
        resp_len = 12;
    } else {
        // Unknown command: return 0.
        resp_buf[0] = 0;
        resp_len = 1;
    }
}

static void i2c_slave_isr(void) {
    i2c_hw_t *hw = i2c_get_hw(I2C_SLAVE_INST);
    uint32_t status = hw->intr_stat;

    // RX_FULL: received data from master (command byte).
    if (status & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        uint8_t cmd = (uint8_t)(hw->data_cmd & 0xFF);
        prepare_response(cmd);
    }

    // RD_REQ: master wants to read, provide the first byte.
    if (status & I2C_IC_INTR_STAT_R_RD_REQ_BITS) {
        if (resp_idx < resp_len) {
            hw->data_cmd = resp_buf[resp_idx++];
        } else {
            hw->data_cmd = 0;
        }
        (void)hw->clr_rd_req;
    }

    // TX_EMPTY: master is clocking out more bytes, provide the next byte.
    if (status & I2C_IC_INTR_STAT_R_TX_EMPTY_BITS) {
        if (resp_idx < resp_len) {
            hw->data_cmd = resp_buf[resp_idx++];
        }
    }

    // Clear all interrupts.
    (void)hw->clr_intr;
}

void i2c_slave_init(void) {
    gpio_init(I2C_SDA_PIN);
    gpio_init(I2C_SCL_PIN);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    // Initialize I2C peripheral; speed is set by the master.
    i2c_init(I2C_SLAVE_INST, 100000);
    i2c_set_slave_mode(I2C_SLAVE_INST, true, I2C_SLAVE_ADDR);

    i2c_hw_t *hw = i2c_get_hw(I2C_SLAVE_INST);
    // Enable RX_FULL, RD_REQ and TX_EMPTY interrupts.
    hw->intr_mask = I2C_IC_INTR_MASK_M_RX_FULL_BITS |
                    I2C_IC_INTR_MASK_M_RD_REQ_BITS |
                    I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;

    int irq = I2C0_IRQ;
    irq_set_exclusive_handler(irq, i2c_slave_isr);
    irq_set_enabled(irq, true);
}

void i2c_slave_poll(void) {
    // Fully interrupt-driven; nothing to do here.
}
