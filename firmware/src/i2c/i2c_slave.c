#include "i2c/i2c_slave.h"

#include "control/control_iface.h"
#include "i2c/i2c_control_map.h"
#include "pwmdriver/pwm_driver.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include <stdbool.h>
#include <string.h>

// I2C0 slave on GPIO 16 (SDA) / 17 (SCL).
#define I2C_SLAVE_INST    i2c0
#define I2C_SLAVE_ADDR    0x40
#define I2C_SDA_PIN       16
#define I2C_SCL_PIN       17

#define I2C_REQ_BUF_SIZE  9
#define RESP_BUF_SIZE     64

static uint8_t req_buf[I2C_REQ_BUF_SIZE];
static uint8_t req_len = 0;
static uint8_t req_expected_len = 0;
static volatile bool req_pending = false;
static volatile uint8_t req_pending_reg = 0;
static uint8_t req_pending_payload[I2C_REQ_BUF_SIZE - 1u];
static volatile uint8_t req_pending_payload_len = 0;
static volatile uint8_t last_status = (uint8_t)PWM_DRIVER_RESULT_OK;

static uint8_t resp_buf[RESP_BUF_SIZE];
static uint8_t resp_len = 0;
static uint8_t resp_idx = 0;

static void prepare_response(uint8_t reg) {
    resp_idx = 0;
    if (!i2c_control_map_read_register(reg, last_status, resp_buf, &resp_len)) {
        resp_buf[0] = (uint8_t)PWM_DRIVER_RESULT_INVALID;
        resp_len = 1u;
    }
}

static void reset_request_capture(void) {
    req_len = 0u;
    req_expected_len = 0u;
}

static void capture_request_byte(uint8_t byte) {
    if (req_len == 0u) {
        req_expected_len = i2c_control_map_expected_write_length(byte);
        resp_len = 0u;
        resp_idx = 0u;
        if ((req_expected_len == 0u) || (req_expected_len > I2C_REQ_BUF_SIZE)) {
            last_status = (uint8_t)PWM_DRIVER_RESULT_INVALID;
            reset_request_capture();
            return;
        }
    }

    if (req_len >= I2C_REQ_BUF_SIZE) {
        last_status = (uint8_t)PWM_DRIVER_RESULT_INVALID;
        reset_request_capture();
        return;
    }

    req_buf[req_len++] = byte;

    if (req_len == req_expected_len) {
        if (req_expected_len > 1u) {
            if (!req_pending) {
                req_pending_reg = req_buf[0];
                req_pending_payload_len = (uint8_t)(req_expected_len - 1u);
                memcpy((void *)req_pending_payload, &req_buf[1], req_pending_payload_len);
                req_pending = true;
                last_status = (uint8_t)PWM_DRIVER_RESULT_BUSY;
            } else {
                last_status = (uint8_t)PWM_DRIVER_RESULT_BUSY;
            }
        }

        prepare_response(req_buf[0]);

        reset_request_capture();
    }
}

static void i2c_slave_isr(void) {
    i2c_hw_t *hw = i2c_get_hw(I2C_SLAVE_INST);
    uint32_t status = hw->intr_stat;

    // RX_FULL: received data from master (command byte).
    if (status & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        uint8_t byte = (uint8_t)(hw->data_cmd & 0xFF);
        capture_request_byte(byte);
    }

    // RD_REQ: master wants to read, provide the first byte.
    if (status & I2C_IC_INTR_STAT_R_RD_REQ_BITS) {
        if ((resp_len == 0u) && (req_len == 1u)) {
            prepare_response(req_buf[0]);
            reset_request_capture();
        }
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

    reset_request_capture();
    resp_len = 0u;
    resp_idx = 0u;
}

void i2c_slave_poll(void) {
    if (req_pending) {
        uint8_t reg = req_pending_reg;
        uint8_t payload[I2C_REQ_BUF_SIZE - 1u];
        uint8_t payload_len = req_pending_payload_len;

        memcpy(payload, (const void *)req_pending_payload, payload_len);
        last_status = (uint8_t)i2c_control_map_execute_write(reg, payload, payload_len);
        req_pending = false;
    }
}