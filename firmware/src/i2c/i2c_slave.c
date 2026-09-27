#include "i2c/i2c_slave.h"

#include "config/i2c_config.h"
#include "device_api/device_api.h"
#include "i2c/i2c_control_map.h"
#include "i2c/i2c_irq_gate.h"
#include "i2c/i2c_write_queue.h"
#include "pwmdriver/pwm_driver.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include <stdbool.h>
#include <string.h>

_Static_assert(I2C_WRITE_STATUS_QUEUED == (int)PWM_DRIVER_RESULT_BUSY,
               "queued write status must match PWM_DRIVER_RESULT_BUSY");
_Static_assert(I2C_WRITE_STATUS_REJECTED == (int)PWM_DRIVER_RESULT_UNAVAILABLE,
               "rejected write status must match PWM_DRIVER_RESULT_UNAVAILABLE");

#define I2C_REQ_BUF_SIZE 9
#define RESP_BUF_SIZE 64

static uint8_t req_buf[I2C_REQ_BUF_SIZE];
static uint8_t req_len = 0;
static uint8_t req_expected_len = 0;
static volatile bool req_in_error = false;
static i2c_write_queue_t write_queue;
static volatile uint8_t last_status[UINT8_MAX + 1u];
static volatile uint32_t status_epoch[UINT8_MAX + 1u];

static uint8_t resp_buf[RESP_BUF_SIZE];
static uint8_t resp_len = 0;
static uint8_t resp_idx = 0;
static volatile uint8_t response_reg = 0;
static volatile bool response_needed = false;
static volatile bool response_ready = false;
static volatile bool irq_deferred = false;
static int i2c_irq_number = -1;
static bool i2c_started = false;
static bool i2c_read_open = false;
static i2c_irq_gate_t i2c_gate;

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
            uint32_t epoch = ++status_epoch[reg];
            bool queued = i2c_write_queue_push(&write_queue, reg, &req_buf[1], payload_len, epoch);
            // A full queue drops this payload and finishes as UNAVAILABLE so the status read does not wait.
            // The epoch keeps an older completion from replacing this newer status.
            last_status[reg] = i2c_write_queue_status(queued);
        }

        request_response(reg);
        reset_request_capture();
    }
}

static uint32_t i2c_slave_intr_mask(void) {
    uint32_t mask = I2C_IC_INTR_MASK_M_RX_FULL_BITS | I2C_IC_INTR_MASK_M_RD_REQ_BITS | I2C_IC_INTR_MASK_M_STOP_DET_BITS;
    mask |= i2c_gate.tx_empty_unmasked ? I2C_IC_INTR_MASK_M_TX_EMPTY_BITS : 0u;
    return mask;
}

static void i2c_slave_apply_tx_mask(i2c_hw_t *hw) {
    hw->intr_mask = i2c_slave_intr_mask();
}

static void i2c_slave_clear_tx_abort(i2c_hw_t *hw) {
    // A transmit abort flushes the FIFOs until IC_CLR_TX_ABRT is read. clr_intr does not release them.
    if ((hw->raw_intr_stat & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS) != 0u) {
        (void)hw->clr_tx_abrt;
    }
}

static void i2c_slave_apply_action(i2c_hw_t *hw, i2c_gate_action_t action, bool read_request) {
    if (action == I2C_GATE_WRITE_BYTE && resp_idx < resp_len) {
        hw->data_cmd = resp_buf[resp_idx++];
    } else if (action == I2C_GATE_PAD_BYTE) {
        hw->data_cmd = 0u;
    }

    if (read_request && action != I2C_GATE_STRETCH && action != I2C_GATE_NONE) {
        (void)hw->clr_rd_req;
    }

    i2c_slave_apply_tx_mask(hw);
    if (!i2c_gate.irq_enabled) {
        irq_set_enabled(i2c_irq_number, false);
        irq_deferred = true;
    }
}

static void i2c_slave_isr(void) {
    i2c_hw_t *hw = i2c_get_hw(I2C_SLAVE_INST);
    uint32_t status = hw->intr_stat;
    bool handled_read = false;

    i2c_slave_clear_tx_abort(hw);

    // RX_FULL: received data from master (command byte).
    if (status & I2C_IC_INTR_STAT_R_RX_FULL_BITS) {
        uint8_t byte = (uint8_t)(hw->data_cmd & 0xFF);
        capture_request_byte(byte);
    }

    if (status & I2C_IC_INTR_STAT_R_STOP_DET_BITS) {
        // A lone register byte of a longer command is a status select, not a new write.
        if (i2c_control_map_is_status_select(req_len, req_expected_len)) {
            request_response(req_buf[0]);
        }
        (void)hw->clr_stop_det;
        reset_request_capture();
        i2c_read_open = false;
        i2c_irq_gate_on_stop(&i2c_gate);
        i2c_slave_apply_tx_mask(hw);
    }

    // RD_REQ: master is clocking out a response. Leave RD_REQ pending when the
    // buffer is not ready so SCL stretches until Core 0 prepares it. TX_EMPTY
    // stays masked unless this byte still has followers; it is level-triggered
    // and clr_intr does not clear it.
    if (status & I2C_IC_INTR_STAT_R_RD_REQ_BITS) {
        i2c_gate_action_t action;

        // Repeated-start read after only the register byte: publish status, do not queue.
        if (i2c_control_map_is_status_select(req_len, req_expected_len)) {
            request_response(req_buf[0]);
            reset_request_capture();
        } else if (i2c_irq_gate_should_refresh(i2c_read_open, response_ready, response_needed, resp_idx, resp_len)) {
            // A later pure read must see the newest status, not a consumed buffer's pad byte.
            request_response(response_reg);
        }
        i2c_read_open = true;
        action = i2c_irq_gate_on_read_request(&i2c_gate, response_ready, response_needed, resp_idx, resp_len);
        i2c_slave_apply_action(hw, action, true);
        handled_read = true;
        if (action == I2C_GATE_STRETCH) {
            return;
        }
    }

    if ((status & I2C_IC_INTR_STAT_R_TX_EMPTY_BITS) != 0u && i2c_gate.tx_empty_unmasked && !handled_read) {
        i2c_gate_action_t action = i2c_irq_gate_on_tx_empty(&i2c_gate, response_ready, resp_idx, resp_len);
        i2c_slave_apply_action(hw, action, false);
    } else if ((status & I2C_IC_INTR_STAT_R_TX_EMPTY_BITS) != 0u && !i2c_gate.tx_empty_unmasked) {
        i2c_slave_apply_tx_mask(hw);
    }

    // Clear interrupts that are not level-triggered.
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

    if (irq_deferred && response_ready) {
        i2c_irq_gate_resume(&i2c_gate);
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
    i2c_irq_gate_init(&i2c_gate);
    // RX_FULL, RD_REQ, and STOP only. TX_EMPTY is unmasked while a read still needs bytes.
    i2c_slave_apply_tx_mask(hw);

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
    i2c_read_open = false;
    for (uint16_t reg = 0u; reg <= UINT8_MAX; ++reg) {
        last_status[reg] = (uint8_t)PWM_DRIVER_RESULT_OK;
        status_epoch[reg] = 0u;
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
        uint8_t result = (uint8_t)i2c_control_map_execute_write(slot.reg, slot.payload, slot.len);

        irq_state = save_and_disable_interrupts();
        if (i2c_write_status_is_current(slot.epoch, status_epoch[slot.reg])) {
            last_status[slot.reg] = result;
        }
        restore_interrupts(irq_state);
    }

    i2c_slave_service_reads();
}
