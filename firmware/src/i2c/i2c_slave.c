#include "i2c/i2c_slave.h"

#include "config/i2c_config.h"
#include "device_api/device_api.h"
#include "i2c/i2c_control_map.h"
#include "i2c/i2c_write_queue.h"
#include "pwmdriver/pwm_driver.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include <stdbool.h>
#include <string.h>

#define I2C_REQ_BUF_SIZE 9
#define RESP_BUF_SIZE 64

static uint8_t req_buf[I2C_REQ_BUF_SIZE];
static uint8_t req_len = 0;
static uint8_t req_expected_len = 0;
static volatile bool req_in_error = false;
static i2c_write_queue_t write_queue;
static volatile uint8_t last_status[UINT8_MAX + 1u];

static uint8_t resp_buf[RESP_BUF_SIZE];
static uint8_t resp_len = 0;
static uint8_t resp_idx = 0;
static volatile uint8_t response_reg = 0;
static volatile bool response_needed = false;
static volatile bool response_ready = false;
static volatile bool irq_deferred = false;
static int i2c_irq_number = -1;
static bool i2c_started = false;

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

static void request_response(uint8_t reg) {
    response_reg = reg;
    response_ready = false;
    response_needed = true;
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
        req_in_error = true;
        return;
    }

    req_buf[req_len++] = byte;

    if (req_len == req_expected_len) {
        uint8_t reg = req_buf[0];
        if (i2c_control_map_is_write_register(reg)) {
            uint8_t payload_len = (uint8_t)(req_expected_len - 1u);
            /* A full queue drops this payload. BUSY tells the master to retry. */
            (void)i2c_write_queue_push(&write_queue, reg, &req_buf[1], payload_len);
            last_status[reg] = (uint8_t)PWM_DRIVER_RESULT_BUSY;
        }

        request_response(reg);
        reset_request_capture();
    }
}

static bool feed_response_byte(i2c_hw_t *hw, bool read_request) {
    if (!response_ready) {
        if (!irq_deferred) {
            irq_set_enabled(i2c_irq_number, false);
            irq_deferred = true;
        }
        return false;
    }

    if (resp_idx < resp_len) {
        hw->data_cmd = resp_buf[resp_idx++];
    } else if (read_request) {
        hw->data_cmd = 0;
    }
    return true;
}

static void i2c_slave_isr(void) {
    i2c_hw_t *hw = i2c_get_hw(I2C_SLAVE_INST);
    uint32_t status = hw->intr_stat;

    // RX_FULL: received data from master (command byte).
    if (status & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        uint8_t byte = (uint8_t)(hw->data_cmd & 0xFF);
        capture_request_byte(byte);
    }

    if (status & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
        (void)hw->clr_stop_det;
        reset_request_capture();
    }

    // RD_REQ / TX_EMPTY: master is clocking out a response. Leave RD_REQ pending
    // when the buffer is not ready so SCL stretches until Core 0 prepares it.
    if (status & (I2C_IC_INTR_STAT_R_RD_REQ_BITS | I2C_IC_INTR_STAT_R_TX_EMPTY_BITS)) {
        bool read_request = (status & I2C_IC_INTR_STAT_R_RD_REQ_BITS) != 0u;
        if (!feed_response_byte(hw, read_request)) {
            return;
        }
        if (read_request) {
            (void)hw->clr_rd_req;
        }
    }

    // Clear all interrupts.
    (void)hw->clr_intr;
}

void i2c_slave_service_reads(void) {
    if (!i2c_started) {
        return;
    }

    while (response_needed) {
        uint8_t reg = response_reg;
        uint32_t irq_state;

        prepare_response(reg);
        irq_state = save_and_disable_interrupts();
        if (!response_needed || response_reg != reg) {
            restore_interrupts(irq_state);
            continue;
        }
        response_ready = true;
        response_needed = false;
        restore_interrupts(irq_state);
        break;
    }

    if (irq_deferred) {
        irq_deferred = false;
        irq_set_enabled(i2c_irq_number, true);
    }
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
    hw->intr_mask = I2C_IC_INTR_MASK_M_RX_FULL_BITS | I2C_IC_INTR_MASK_M_RD_REQ_BITS |
                    I2C_IC_INTR_MASK_M_TX_EMPTY_BITS | I2C_IC_INTR_MASK_M_STOP_DET_BITS;

    i2c_irq_number = I2C_SLAVE_IRQ;
    irq_set_exclusive_handler(i2c_irq_number, i2c_slave_isr);
    irq_set_enabled(i2c_irq_number, true);

    reset_request_capture();
    write_queue = (i2c_write_queue_t){0};
    resp_len = 0u;
    resp_idx = 0u;
    response_needed = false;
    response_ready = false;
    irq_deferred = false;
    for (uint16_t reg = 0u; reg <= UINT8_MAX; ++reg) {
        last_status[reg] = (uint8_t)PWM_DRIVER_RESULT_OK;
    }
    i2c_started = true;
}

void i2c_slave_poll(void) {
    i2c_write_slot_t slot;
    bool has_write;
    uint32_t irq_state;

    if (!i2c_started) {
        return;
    }

    irq_state = save_and_disable_interrupts();
    has_write = i2c_write_queue_pop(&write_queue, &slot);
    restore_interrupts(irq_state);

    if (has_write) {
        last_status[slot.reg] = (uint8_t)i2c_control_map_execute_write(slot.reg, slot.payload, slot.len);
    }

    i2c_slave_service_reads();
}
