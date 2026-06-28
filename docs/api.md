# Internal API

This page documents the C API exposed by the firmware modules. All modules are in `src/`.

---

## `control.h` — Unified Channel Control

Presents 24 logical channels (0..7 hardware, 8..23 software) as a single uniform interface.

**Setters run on Core 0** and push commands to a queue. **`control_process_pending` runs on Core 1** and applies them to hardware. **Getters run on Core 0** and read cached/volatile values.

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `CONTROL_CHANNEL_COUNT` | 24 | Total number of logical channels. |

### Types

```c
typedef enum {
    CTRL_CMD_SET_FREQ = 0,
    CTRL_CMD_SET_DUTY = 1,
    CTRL_CMD_STOP_ALL = 2,
} ctrl_cmd_type_t;

typedef struct {
    uint8_t  type;
    uint8_t  channel;
    float    freq;
    float    duty;
} ctrl_cmd_t;
```

### Functions

#### `void control_init(void)`

Initialize cached values and the command queue. Called on Core 0 before launching Core 1.

---

#### `bool control_set_freq(uint channel, float freq_hz)`

Set the frequency of a logical channel (Core 0). Pushes a `SET_FREQ` command to the queue. Returns `true` if queued, `false` if channel invalid, queue full, or the requested frequency is not representable on the target PWM backend.

- `freq_hz = 0` disables the channel.
- Hardware PWM accepts a finite range roughly bounded by the system clock and slice divider limits.
- Software PWM accepts `0` to `100000 Hz`.

---

#### `bool control_set_duty(uint channel, float duty)`

Set the duty cycle (Core 0). Pushes a `SET_DUTY` command. `duty` is 0.0..1.0. Returns `false` if the channel is invalid, the queue is full, or the duty is not finite.

---

#### `void control_process_pending(void)`

Drain the command queue and apply changes to PWM hardware. **Called on Core 1** from `pwm_core_main()`.

---

#### `float control_get_freq(uint channel)`

Return the requested frequency (Core 0, direct read from cache).

---

#### `float control_get_duty(uint channel)`

Return the current duty cycle (Core 0, direct read from cache).

---

#### `uint32_t control_get_pulse_count(uint channel)`

Return the 32-bit pulse counter (Core 0, volatile read from hw/sw module). Read-only.

---

#### `bool control_is_enabled(uint channel)`

Return `true` if `freq > 0` (Core 0, direct read from cache). No cross-core access needed.

---

#### `void control_stop_all(void)`

Reset cached values immediately and push a `STOP_ALL` command. Core 1 will disable all channels and clear all pulse counters.

---

## `pwm_core.h` — Core 1 PWM Manager

Launches and manages the PWM core.

### Functions

#### `void pwm_core_launch(void)`

Start Core 1 with `pwm_core_main` as entry point. Core 1 initializes PWM hardware, enables interrupts on Core 1, and enters its main loop.

---

#### `bool pwm_core_is_ready(void)`

Returns `true` once Core 1 has finished `hw_pwm_init()` and `sw_pwm_init()` and is processing commands.

---

## `hw_pwm.h` — Hardware PWM

Manages 8 hardware PWM channels using RP2040 PWM slices. **All functions are called on Core 1.**

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

Initialize all 8 PWM slices and enable the wrap interrupt on the calling core (Core 1).

---

#### `bool hw_pwm_set_freq(uint channel, float freq_hz, float duty)`

Configure frequency and duty. Searches all `(TOP, clkdiv)` combinations for the best match. `freq_hz = 0` disables.

---

#### `void hw_pwm_set_duty(uint channel, float duty)`

Update duty cycle without changing frequency.

---

#### `uint32_t hw_pwm_get_pulse_count(uint channel)`

Return the pulse counter (volatile read, safe from Core 0).

---

## `sw_pwm.h` — Software PWM

Manages 16 software PWM channels driven by a 100 kHz timer interrupt on Core 1.

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

Initialize all software PWM GPIOs as outputs and start the 10 µs repeating timer on the calling core (Core 1).

---

#### `bool sw_pwm_set_freq(uint channel, float freq_hz, float duty)`

Set frequency and duty. Uses `save_and_disable_interrupts()` to atomically update the channel struct. `freq_hz = 0` disables.

---

#### `void sw_pwm_set_duty(uint channel, float duty)`

Update duty cycle. Also uses interrupt protection.

---

#### `uint32_t sw_pwm_get_pulse_count(uint channel)`

Return the pulse counter (volatile read, safe from Core 0).

---

## `cmd_parser.h` — Shared Command Parser

Text command parser used by the USB CDC transport on Core 0.

### Functions

#### `void cmd_parser_init(void)` / `void cmd_parser_process_char(char c)` / `void cmd_parser_poll(void)`

Feed characters and execute commands when a newline is received.

---

#### `void print_help(void)` / `void print_status(void)`

Print help text or all-channel status.

---

## `cdc_cmd.h` — USB CDC Transport (Core 0)

#### `void cdc_cmd_init(void)` / `void cdc_cmd_poll(void)`

Initialize and poll the USB CDC interface. `cdc_cmd_poll` calls `tud_task()` and reads incoming characters.

---

## `i2c_slave.h` — I2C Slave Transport (Core 0)

#### `void i2c_slave_init(void)` / `void i2c_slave_poll(void)`

Initialize I2C0 slave (address `0x40`, GPIO 16/17) and poll. The ISR is registered on Core 0.

---

## Thread Safety Summary

| Data | Writer | Reader | Protection |
|------|--------|--------|------------|
| `control.c freqs[]` / `duties[]` | Core 0 | Core 0 | None needed (same core) |
| `hw_pwm.c pulse_counts[]` | Core 1 ISR | Core 0 | `volatile uint32_t` (atomic on ARM) |
| `sw_pwm.c pulse_count` | Core 1 ISR | Core 0 | `volatile uint32_t` (atomic on ARM) |
| `sw_pwm.c channel struct` | Core 1 (set_freq) | Core 1 ISR | `save_and_disable_interrupts()` |
| Command queue | Core 0 (`queue_try_add`) | Core 1 (`queue_try_remove`) | `queue_t` (spinlock-based) |
