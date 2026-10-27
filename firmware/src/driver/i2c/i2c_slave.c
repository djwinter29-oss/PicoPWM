/**
 * @file i2c_slave.c
 * @brief I2C slave protocol and ISR logic; the peripheral is reached through `i2c_slave_hw.h`.
 *
 * - A write is captured byte by byte. A complete command write is queued for Core 0
 *   and its status reads back as QUEUED until `i2c_slave_poll()` finishes it.
 * - A complete write or a lone register byte selects the register for the next read.
 * - Each read transaction rebuilds the response in the ISR, so a status read is always fresh.
 *   Bytes past the end of the response are zero pads.
 * - The ISR runs on Core 0, as do `i2c_slave_poll()` and everything that touches this state,
 *   so masking interrupts is enough to guard the queue and the status tables.
 */

#include "driver/i2c/i2c_slave.h"

#include "driver/i2c/i2c_slave_hw.h"
#include "driver/i2c/i2c_write_queue.h"
#include "hardware/sync.h"

/** @brief Longest write: the register byte plus the largest queued payload. */
#define I2C_REQ_BUF_SIZE (I2C_WRITE_QUEUE_PAYLOAD + 1u)
/** @brief Capacity of one read response. */
#define I2C_RESP_BUF_SIZE 64u

static const i2c_slave_ops_t *slave_ops;
static uint8_t slave_address;
static bool slave_started;

static uint8_t req_buf[I2C_REQ_BUF_SIZE]; /**< Bytes of the write being captured, register first. */
static uint8_t req_len;                   /**< Bytes captured so far. */
static uint8_t req_expected_len;          /**< Full length of the command selected by the first byte. */
static bool req_in_error;                 /**< Drop bytes until STOP after a malformed command. */

static i2c_write_queue_t write_queue;
static uint8_t last_status[UINT8_MAX + 1u]; /**< Status a master reads back, per register. */
/*
 * ponytail: the 8-bit epoch can only wrap onto an in-flight value after 256 writes to one register,
 * and the queue holds 4, so a stale completion can never match. Both tables are indexed by the raw
 * register byte (512 B); index by write-register slot if RAM ever gets tight.
 */
static uint8_t status_epoch[UINT8_MAX + 1u]; /**< Attempt counter per register; a stale completion is dropped. */

static uint8_t resp_buf[I2C_RESP_BUF_SIZE];
static uint8_t resp_len;
static uint8_t resp_idx;
static uint8_t response_reg;    /**< Register the next read transaction answers. */
static bool response_reg_valid; /**< False until a register has been selected. */
static bool read_open;          /**< This transaction already built its response. */

static bool is_status_select(void) {
    // A lone register byte of a longer command is a status select, not a new write.
    return req_len == 1u && req_expected_len > 1u;
}

static void reset_request_capture(void) {
    req_len = 0u;
    req_expected_len = 0u;
    req_in_error = false;
}

static void select_response(uint8_t reg) {
    response_reg = reg;
    response_reg_valid = true;
    read_open = false;
}

static void build_response(void) {
    resp_idx = 0u;
    resp_len = 0u;
    if (!response_reg_valid) {
        return;
    }
    if (!slave_ops->read(response_reg, last_status[response_reg], resp_buf, I2C_RESP_BUF_SIZE, &resp_len)) {
        resp_buf[0] = (uint8_t)I2C_STATUS_INVALID;
        resp_len = 1u;
    }
}

static void send_response_byte(void) {
    i2c_slave_hw_tx_push(resp_idx < resp_len ? resp_buf[resp_idx++] : 0u);
    i2c_slave_hw_set_tx_empty_irq(resp_idx < resp_len);
}

static void capture_request_byte(uint8_t byte) {
    if (req_in_error) {
        return;
    }

    if (req_len == 0u) {
        req_expected_len = slave_ops->write_length(byte);
        if (req_expected_len == 0u || req_expected_len > I2C_REQ_BUF_SIZE) {
            last_status[byte] = (uint8_t)I2C_STATUS_INVALID;
            reset_request_capture();
            req_in_error = true;
            return;
        }
    }

    req_buf[req_len++] = byte;
    if (req_len < req_expected_len) {
        return;
    }

    if (slave_ops->is_write(req_buf[0])) {
        uint8_t reg = req_buf[0];
        uint8_t epoch = (uint8_t)(status_epoch[reg] + 1u);
        bool queued;

        status_epoch[reg] = epoch;
        queued = i2c_write_queue_push(&write_queue, reg, &req_buf[1], (uint8_t)(req_expected_len - 1u), epoch);
        // A full queue drops this write and finishes as REJECTED so the status read does not wait.
        last_status[reg] = (uint8_t)(queued ? I2C_STATUS_QUEUED : I2C_STATUS_REJECTED);
    }
    select_response(req_buf[0]);
    reset_request_capture();
}

static void handle_stop(void) {
    if (is_status_select()) {
        select_response(req_buf[0]);
    }
    i2c_slave_hw_clear(I2C_SLAVE_EVENT_STOP);
    reset_request_capture();
    read_open = false;
    i2c_slave_hw_set_tx_empty_irq(false);
}

static void handle_read_request(void) {
    // Repeated-start read after only the register byte: answer with its status, do not queue.
    if (is_status_select()) {
        select_response(req_buf[0]);
        reset_request_capture();
    }
    if (!read_open) {
        read_open = true;
        build_response();
    }
    send_response_byte();
    i2c_slave_hw_clear(I2C_SLAVE_EVENT_RD_REQ);
}

static void handle_tx_empty(void) {
    if (resp_idx < resp_len) {
        send_response_byte();
    } else {
        i2c_slave_hw_set_tx_empty_irq(false);
    }
}

static void i2c_slave_isr(void) {
    /*
     * ponytail: eight passes cover one command FIFO plus a STOP that lands while this handler
     * is already running. A master that keeps the FIFO non-empty past that window leaves RX_FULL
     * asserted and the level-triggered IRQ re-enters, so nothing is lost. The 16-byte pop bound
     * is the RX FIFO depth and keeps a stuck FIFO from spinning the ISR.
     */
    for (uint32_t pass = 0u; pass < 8u; ++pass) {
        uint32_t events;
        uint8_t byte;

        for (uint32_t popped = 0u; popped < 16u && i2c_slave_hw_rx_pop(&byte); ++popped) {
            capture_request_byte(byte);
        }

        events = i2c_slave_hw_events();
        if (events == 0u) {
            break;
        }
        if ((events & I2C_SLAVE_EVENT_STOP) != 0u) {
            handle_stop();
        }
        if ((events & I2C_SLAVE_EVENT_RD_REQ) != 0u) {
            handle_read_request();
        } else if ((events & I2C_SLAVE_EVENT_TX_EMPTY) != 0u) {
            handle_tx_empty();
        }
    }
}

uint8_t i2c_slave_address(void) {
    return slave_address;
}

void i2c_slave_init(uint8_t address, const i2c_slave_ops_t *ops) {
    slave_ops = ops;
    slave_address = address;
    reset_request_capture();
    write_queue = (i2c_write_queue_t){0};
    resp_len = 0u;
    resp_idx = 0u;
    response_reg_valid = false;
    read_open = false;
    for (uint16_t reg = 0u; reg <= UINT8_MAX; ++reg) {
        last_status[reg] = (uint8_t)I2C_STATUS_OK;
        status_epoch[reg] = 0u;
    }
    i2c_slave_hw_init(address, i2c_slave_isr);
    slave_started = true;
}

void i2c_slave_poll(void) {
    i2c_write_slot_t slot;
    uint32_t irq_state;
    bool has_write;

    if (!slave_started) {
        return;
    }

    irq_state = save_and_disable_interrupts();
    has_write = i2c_write_queue_pop(&write_queue, &slot);
    restore_interrupts(irq_state);
    if (!has_write) {
        return;
    }

    uint8_t result = slave_ops->execute(slot.reg, slot.payload, slot.len);

    irq_state = save_and_disable_interrupts();
    if (slot.epoch == status_epoch[slot.reg]) {
        last_status[slot.reg] = result;
    }
    restore_interrupts(irq_state);
}
