# Internal API

This page documents the C API exposed by the firmware modules. All modules are in `src/`.

---

## `control.h` — Unified Channel Control

Presents 24 logical channels (0..7 hardware, 8..23 software) as a single uniform interface.

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `CONTROL_CHANNEL_COUNT` | 24 | Total number of logical channels. |

### Functions

#### `void control_init(void)`

Initialize all logical channels to defaults:
- `freq = 0.0f` (off)
- `duty = 0.5f` (50%)
- `pulse_count = 0`

Call after `hw_pwm_init()` and `sw_pwm_init()`.

---

#### `bool control_set_freq(uint channel, float freq_hz)`

Set the frequency of a logical channel in Hz. Returns `true` on success, `false` if channel is invalid.

- `freq_hz = 0` disables the channel.
- For hardware channels, the actual frequency is quantized by the PWM clock divider and wrap value.
- For software channels, the frequency is quantized to the 10 µs tick rate.

---

#### `bool control_set_duty(uint channel, float duty)`

Set the duty cycle of a logical channel. `duty` is in the range 0.0..1.0. Values outside this range are clamped.

---

#### `float control_get_freq(uint channel)`

Return the requested frequency of the channel in Hz. Returns `0.0f` for invalid channels.

---

#### `float control_get_duty(uint channel)`

Return the current duty cycle of the channel (0.0..1.0).

---

#### `uint32_t control_get_pulse_count(uint channel)`

Return the current 32-bit pulse count. The counter wraps to zero on overflow.

**Note:** `pulse_count` is read-only at the control level. It cannot be set directly from external commands.

---

#### `bool control_is_enabled(uint channel)`

Return `true` if the channel is currently enabled (frequency > 0).

---

#### `void control_stop_all(void)`

Reset every channel to the power-up default state:
- `freq = 0 Hz` (disabled)
- `duty = 50%`
- `pulse_count = 0`

This is the only way to reset pulse counters from the external command interface.

---

## `hw_pwm.h` — Hardware PWM

Manages 8 hardware PWM channels using RP2040 PWM slices.

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `HW_PWM_COUNT` | 8 | Number of hardware PWM channels. |

### GPIO Mapping

| Channel | GPIO | PWM Slice | PWM Channel |
|---------|------|-----------|-------------|
| 0 | 0 | 0 | A |
| 1 | 2 | 1 | A |
| 2 | 4 | 2 | A |
| 3 | 6 | 3 | A |
| 4 | 8 | 4 | A |
| 5 | 10 | 5 | A |
| 6 | 12 | 6 | A |
| 7 | 14 | 7 | A |

### Functions

#### `void hw_pwm_init(void)`

Initialize all 8 PWM slices and enable the wrap interrupt for pulse counting. Output is disabled by default.

---

#### `bool hw_pwm_set_freq(uint channel, float freq_hz, float duty)`

Configure frequency and duty for a hardware channel. Searches for the best `(TOP, clkdiv)` combination to match the requested frequency.

- `freq_hz = 0` disables the channel.
- Valid channels: 0..7.

---

#### `void hw_pwm_set_duty(uint channel, float duty)`

Update duty cycle without changing frequency.

---

#### `void hw_pwm_enable(uint channel, bool enable)`

Enable or disable the PWM slice output.

---

#### `float hw_pwm_get_actual_freq(uint channel)`

Return the actual configured frequency (after quantization), not the requested frequency.

---

#### `float hw_pwm_get_duty(uint channel)`

Return the current duty cycle.

---

#### `bool hw_pwm_is_enabled(uint channel)`

Return `true` if the channel is enabled.

---

#### `uint32_t hw_pwm_get_pulse_count(uint channel)`

Return the pulse counter. The counter is incremented in the PWM wrap interrupt and wraps on overflow.

---

## `sw_pwm.h` — Software PWM

Manages 16 software PWM channels driven by a 100 kHz periodic timer interrupt.

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `SW_PWM_COUNT` | 16 | Number of software PWM channels. |

### GPIO Mapping

| Channel | GPIO |
|---------|------|
| 0 | 1 |
| 1 | 3 |
| 2 | 5 |
| 3 | 7 |
| 4 | 9 |
| 5 | 11 |
| 6 | 13 |
| 7 | 15 |
| 8 | 18 |
| 9 | 19 |
| 10 | 20 |
| 11 | 21 |
| 12 | 22 |
| 13 | 25 |
| 14 | 26 |
| 15 | 27 |

### Functions

#### `void sw_pwm_init(void)`

Initialize all software PWM GPIOs as outputs and start the 10 µs repeating timer.

---

#### `bool sw_pwm_set_freq(uint channel, float freq_hz, float duty)`

Set frequency and duty for a software channel.

- `freq_hz = 0` disables the channel.
- Valid channels: 0..15.
- Frequency range: 0.1 Hz to 1 kHz.

---

#### `void sw_pwm_set_duty(uint channel, float duty)`

Update duty cycle without changing the configured period.

---

#### `void sw_pwm_enable(uint channel, bool enable)`

Enable or disable the channel output.

---

#### `float sw_pwm_get_freq(uint channel)`

Return the requested frequency (not the quantized frequency).

---

#### `float sw_pwm_get_duty(uint channel)`

Return the current duty cycle.

---

#### `bool sw_pwm_is_enabled(uint channel)`

Return `true` if the channel is enabled.

---

#### `uint32_t sw_pwm_get_pulse_count(uint channel)`

Return the pulse counter. The counter is incremented at the end of every PWM period in the timer interrupt and wraps on overflow.

---

## `cmd_parser.h` — Shared Command Parser

Text command parser used by the USB CDC transport. Processes characters one at a time or by polling `stdio`.

### Functions

#### `void cmd_parser_init(void)`

Initialize the parser state.

---

#### `void cmd_parser_process_char(char c)`

Feed a single character into the command parser. A command is executed when a newline is received.

---

#### `void cmd_parser_poll(void)`

Poll `stdio` for input and feed characters to the parser.

---

#### `void print_help(void)`

Print the command help text.

---

#### `void print_status(void)`

Print the status of all 24 logical channels.

---

## `cdc_cmd.h` — USB CDC Transport

Wraps the shared command parser over the USB CDC serial interface.

### Functions

#### `void cdc_cmd_init(void)`

Initialize the USB CDC command interface. Calls `cmd_parser_init()`.

---

#### `void cdc_cmd_poll(void)`

Poll for incoming USB data and process it.

---

## `i2c_slave.h` — I2C Slave Transport

Implements an interrupt-driven I2C slave for read-only status queries.

### Functions

#### `void i2c_slave_init(void)`

Initialize I2C0 as a slave on GPIO 16/17 at address `0x40`.

---

#### `void i2c_slave_poll(void)`

Poll / maintain the I2C slave state. Call from main loop.

---

## Thread Safety Notes

- The hardware PWM pulse counts and software PWM state are updated from interrupts.
- The control API is called from the main loop (CDC commands). No explicit locking is implemented.
- For critical applications, consider wrapping multi-byte reads in `__disable_irq()` / `__enable_irq()`.
- The `stop` command resets both requested state and pulse counters, so it should not be interrupted by other control calls.
