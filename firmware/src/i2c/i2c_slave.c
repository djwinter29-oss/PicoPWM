#include "i2c/i2c_slave.h"

#include "config/i2c_config.h"
#include "device_api/device_api.h"
#include "i2c/i2c_control_map.h"
#include "pwmdriver/pwm_driver.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include <stdbool.h>
#include <string.h>

#define I2C_REQ_BUF_SIZE  9
#define RESP_BUF_SIZE     64

static uint8_t req_buf[I2C_REQ_BUF_SIZE];
static uint8_t req_len = 0;
static uint8_t req_expected_len = 0;
static volatile bool req_in_error = false;
static volatile bool req_pending = false;
static volatile uint8_t req_pending_reg = 0;
static uint8_t req_pending_payload[I2C_REQ_BUF_SIZE - 1u];
static volatile uint8_t req_pending_payload_len = 0;
static volatile uint8_t last_status[UINT8_MAX + 1u];

static uint8_t resp_buf[RESP_BUF_SIZE];
static uint8_t resp_len = 0;
static uint8_t resp_idx = 0;

static void prepare_response(uint8_t reg) {
    resp_idx = 0;
    if (!i2c_control_map_read_register(reg, last_status[reg], resp_buf, RESP_BUF_SIZE, &resp_len)) {
        resp_buf[0] = (uint8_t)PWM_DRIVER_RESULT_INVALID;
        resp_len = 1u;
    }
}

static void reset_request_capture(void) {
    req_len = 0u;
    req_expected_len = 0u;
    req_in_error = false;
}

static void capture_request_byte(uint8_t byte) {
    // If a previous command was malformed, drop all bytes until bus reset.
    if (req_in_error) {
        return;
    }

    if (req_len == 0u) {
        req_expected_len = i2c_control_map_expected_write_length(byte);
        resp_len = 0u;
        resp_idx = 0u;
        if ((req_expected_len == 0u) || (req_expected_len > I2C_REQ_BUF_SIZE)) {
            last_status[byte] = (uint8_t)PWM_DRIVER_RESULT_INVALID;
            reset_request_capture();
            req_in_error = true;
            return;
        }
    }

    if (req_len >= I2C_REQ_BUF_SIZE) {
        last_status[req_buf[0]] = (uint8_t)PWM_DRIVER_RESULT_INVALID;
        reset_request_capture();
        return;
    }

    req_buf[req_len++] = byte;

    if (req_len == req_expected_len) {
        if (i2c_control_map_is_write_register(req_buf[0])) {
            if (!req_pending) {
                req_pending_reg = req_buf[0];
                req_pending_payload_len = (uint8_t)(req_expected_len - 1u);
                memcpy((void *)req_pending_payload, &req_buf[1], req_pending_payload_len);
                req_pending = true;
                last_status[req_buf[0]] = (uint8_t)PWM_DRIVER_RESULT_BUSY;
            } else {
                last_status[req_buf[0]] = (uint8_t)PWM_DRIVER_RESULT_BUSY;
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

    if (status & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
        (void)hw->clr_stop_det;
        reset_request_capture();
    }

    // Clear all interrupts.
    (void)hw->clr_intr;
}

void i2c_slave_init(uint8_t address) {
    gpio_init(I2C_SDA_PIN);
    gpio_init(I2C_SCL_PIN);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    // Initialize I2C peripheral for slave mode.
    // The clock speed parameter configures internal timing (filtering, edge detection);
    // the actual bus speed is set by the I2C master. Ensure this matches the expected
    // master clock to avoid timing violations.
    i2c_init(I2C_SLAVE_INST, I2C_CLOCK_SPEED);
    i2c_set_slave_mode(I2C_SLAVE_INST, true, address);

    i2c_hw_t *hw = i2c_get_hw(I2C_SLAVE_INST);
    // Enable RX_FULL, RD_REQ and TX_EMPTY interrupts.
    hw->intr_mask = I2C_IC_INTR_MASK_M_RX_FULL_BITS |
                    I2C_IC_INTR_MASK_M_RD_REQ_BITS |
                    I2C_IC_INTR_MASK_M_TX_EMPTY_BITS |
                    I2C_IC_INTR_MASK_M_STOP_DET_BITS;

    int irq = I2C_SLAVE_IRQ;
    irq_set_exclusive_handler(irq, i2c_slave_isr);
    irq_set_enabled(irq, true);

    reset_request_capture();
    resp_len = 0u;
    resp_idx = 0u;
    for (uint16_t reg = 0u; reg <= UINT8_MAX; ++reg) {
        last_status[reg] = (uint8_t)PWM_DRIVER_RESULT_OK;
    }
}

void i2c_slave_poll(void) {
    if (req_pending) {
        // Capture pending request state to locals before clearing pending flag.
        // Safe on RP2040 and RP2350: copy the volatile slot to locals before
        // clearing the pending flag, so the ISR and poller never share mutable
        // payload storage while the deferred command executes.
        uint8_t reg = req_pending_reg;
        uint8_t payload[I2C_REQ_BUF_SIZE - 1u];
        uint8_t payload_len = req_pending_payload_len;

        memcpy(payload, (const void *)req_pending_payload, payload_len);
        // Clear pending before execute so ISR can queue the next write immediately.
        req_pending = false;
        last_status[reg] = (uint8_t)i2c_control_map_execute_write(reg, payload, payload_len);
    }
}