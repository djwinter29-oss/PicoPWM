/**
 * @file i2c_slave.c
 * @brief Generic I2C transaction framing and ISR logic.
 *
 * The protocol callbacks own frame interpretation and response contents. This
 * module only captures transaction bytes and streams the callback's response.
 */

#include "driver/i2c/i2c_slave.h"

#include "driver/i2c/i2c_slave_hw.h"

#define I2C_SLAVE_RX_FIFO_DEPTH 16u
#define I2C_SLAVE_ISR_MAX_PASSES 8u

static const i2c_slave_ops_t *slave_ops;
static uint8_t slave_address;

static uint8_t req_buf[I2C_SLAVE_FRAME_CAPACITY]; /**< Bytes in the current write transaction. */
static uint8_t req_len;                            /**< Stored bytes in the current transaction. */
static bool req_overflow;                          /**< Transaction exceeded the fixed frame buffer. */
static uint8_t resp_buf[I2C_SLAVE_RESPONSE_CAPACITY];
static uint8_t resp_len;
static uint8_t resp_idx;
static bool read_open;          /**< This transaction already built its response. */

static void reset_request_capture(void) {
    req_len = 0u;
    req_overflow = false;
}

static void send_response_byte(void) {
    i2c_slave_hw_tx_push(resp_idx < resp_len ? resp_buf[resp_idx++] : 0u);
    i2c_slave_hw_set_tx_empty_irq(resp_idx < resp_len);
}

static void capture_request_byte(uint8_t byte) {
    if (req_len < I2C_SLAVE_FRAME_CAPACITY) {
        req_buf[req_len++] = byte;
    } else {
        req_overflow = true;
    }
}

static void finish_request(void) {
    if (req_len > 0u || req_overflow) {
        slave_ops->write_frame(req_buf, req_len, req_overflow);
    }
    reset_request_capture();
}

static void handle_transaction_end(uint32_t event) {
    finish_request();
    i2c_slave_hw_clear(event);
    read_open = false;
    i2c_slave_hw_set_tx_empty_irq(false);
}

static void handle_read_request(void) {
    finish_request();
    if (!read_open) {
        resp_idx = 0u;
        resp_len = 0u;
        read_open = true;
        if (!slave_ops->read_response(resp_buf, I2C_SLAVE_RESPONSE_CAPACITY, &resp_len) ||
            resp_len > I2C_SLAVE_RESPONSE_CAPACITY) {
            resp_len = 0u;
        }
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

static void handle_tx_abort(void) {
    resp_len = 0u;
    resp_idx = 0u;
    read_open = false;
    i2c_slave_hw_set_tx_empty_irq(false);
    i2c_slave_hw_clear(I2C_SLAVE_EVENT_TX_ABORT);
}

static void i2c_slave_isr(void) {
    /*
     * ponytail: the pass cap covers a FIFO drain plus events arriving during the ISR. If more work
     * remains, the level-triggered IRQ re-enters. The per-pass pop cap matches the RX FIFO depth.
     */
    for (uint32_t pass = 0u; pass < I2C_SLAVE_ISR_MAX_PASSES; ++pass) {
        uint32_t events;
        uint8_t byte;
        bool first_data_byte;

        for (uint32_t popped = 0u;
             popped < I2C_SLAVE_RX_FIFO_DEPTH && i2c_slave_hw_rx_pop(&byte, &first_data_byte); ++popped) {
            if (first_data_byte) {
                finish_request();
            }
            capture_request_byte(byte);
        }

        events = i2c_slave_hw_events();
        if (events == 0u) {
            break;
        }
        if ((events & I2C_SLAVE_EVENT_TX_ABORT) != 0u) {
            handle_tx_abort();
        }
        if ((events & I2C_SLAVE_EVENT_STOP) != 0u) {
            handle_transaction_end(I2C_SLAVE_EVENT_STOP);
        }
        if ((events & I2C_SLAVE_EVENT_RESTART) != 0u) {
            handle_transaction_end(I2C_SLAVE_EVENT_RESTART);
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
    resp_len = 0u;
    resp_idx = 0u;
    read_open = false;
    i2c_slave_hw_init(address, i2c_slave_isr);
}
