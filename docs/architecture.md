# Architecture

PicoPWM targets a low-cost Raspberry Pi Pico platform with a shared external interface and two firmware roles. The intended hardware targets are:

- **Raspberry Pi Pico (RP2040)**
- **Raspberry Pi Pico 2 (RP2350)**

- **PWM generator** for producing 24 logical PWM channels.
- **PWM monitoring** for measurement-oriented use while keeping the same external connector mapping.

The control surface is the same in both roles: USB CDC CLI for interactive control and I2C for host integration.

For the generator role, the intended backend split is:

- **8 hardware PWM channels** on the MCU PWM slice **channel B** pins.
- **8 PIO PWM channels** for good timing accuracy on a second bank of outputs.
- **8 software PWM channels** for lower-frequency outputs.

The hardware channels intentionally use **channel B** so the physical pins remain aligned with monitoring-oriented input use cases.

## System Overview

## Dual-Core Overview

```
┌─ Core 0 — Communication ───────────────────────────┐
│                                                     │
│  main.c                                             │
│    ├── stdio_init_all()        USB CDC              │
│    ├── control_init()          init cache + queue   │
│    ├── pwm_core_launch()       start Core 1         │
│    ├── cdc_cmd_init()          CDC command reader   │
│    ├── i2c_slave_init()        I2C ISR on Core 0    │
│    └── main loop: cdc_cmd_poll() + i2c_slave_poll() │
│                                                     │
│  cmd_parser.c   text command parser                 │
│  cdc_cmd.c      USB CDC transport                   │
│  i2c_slave.c    I2C slave ISR (read-only queries)   │
│                                                     │
│  control.c (setters)  push commands to queue        │
│  control.c (getters)  read cached values + pulse    │
└─────────────────────────────────────────────────────┘
          │                          │
          │   command queue           │ volatile reads
          │   (queue_t, 32 slots)     │ (pulse_count)
          ▼                          ▼
┌─ Core 1 — PWM Management ───────────────────────────┐
│                                                     │
│  pwm_core.c                                         │
│    ├── hw_pwm_init()          PWM slices + wrap IRQ │
│    ├── sw_pwm_init()          timer IRQ (100 kHz)   │
│    └── main loop:                                  │
│          control_process_pending()  drain queue     │
│          __wfi()                    sleep until IRQ │
│                                                     │
│  hw_pwm.c   8 HW PWM channels + wrap ISR            │
│  pio_pwm.c  8 PIO PWM channels                      │
│  sw_pwm.c   8 SW PWM channels + timer ISR           │
│                                                     │
│  control.c (process_pending)  apply freq/duty/stop  │
└─────────────────────────────────────────────────────┘
          │
          ▼
┌─ Hardware ──────────────────────────────────────────┐
│  MCU PWM slices 0-7 (channel B each)                │
│  MCU PIO state machines for 8 channels              │
│  MCU hardware timer (10 µs repeating)               │
│  24 logical PWM pins                                │
│  GPIO 16/17 (I2C0 slave)                            │
│  USB (CDC)                                          │
└─────────────────────────────────────────────────────┘
```

## Why Dual-Core?

| Concern | Single-Core Problem | Dual-Core Solution |
|---------|--------------------|--------------------|
| SW PWM ISR at 100 kHz | Steals CPU from USB/I2C | Core 1 handles it entirely |
| Mixed backends | Different timing engines complicate control | One logical channel model hides backend differences |
| HW PWM wrap ISR | Adds interrupt load to comm | Core 1 absorbs it |
| USB CDC responsiveness | Can stall during ISR | Core 0 is always free |
| I2C slave latency | Can miss bytes during PWM ISR | Core 0 ISR is never preempted by PWM |

## Command Queue

Core 0 and Core 1 communicate through a lock-free `queue_t` (from `pico/util/queue.h`):

```
Core 0 (CDC/I2C command)                Core 1 (pwm_core main loop)
  ┌───────────────┐                       ┌──────────────────────┐
  │ control_set_  │  queue_try_add()      │ control_process_     │
  │ freq/duty()   │ ──────────────────►   │ pending()            │
  │ control_      │  queue_try_add()      │   hw_pwm_set_freq()  │
  │ stop_all()    │ ──────────────────►   │   sw_pwm_set_freq()  │
  └───────────────┘                       │   hw_pwm_set_duty()  │
                                          │   sw_pwm_set_duty()  │
                                          │   stop + clear pulses│
                                          └──────────────────────┘
```

- Queue capacity: 32 commands
- Non-blocking: `queue_try_add` drops if full (returns `false`)
- Core 1 drains the queue every ~10 µs (woken by timer ISR)

## Data Flow for Reads

Reads (`get`, `status`, I2C queries) do **not** go through the queue. Core 0 reads directly:

| Property | Source | Cross-core? | Safe? |
|----------|--------|-------------|-------|
| `freq` | `control.c` cached `freqs[]` | No (Core 0 only) | Yes |
| `duty` | `control.c` cached `duties[]` | No (Core 0 only) | Yes |
| `pulse_count` | `hw_pwm.c` / `sw_pwm.c` volatile | Yes | Yes (atomic 32-bit read) |
| `enabled` | Derived from `freqs[ch] > 0` | No (Core 0 only) | Yes |

## Channel Classes

| Logical Channels | Backend | GPIOs | Notes |
|------------------|---------|-------|-------|
| `0..7` | Hardware PWM | 1, 3, 5, 7, 9, 11, 13, 15 | Slice 0..7, channel B |
| `8..15` | PIO PWM | 0, 2, 4, 6, 8, 10, 12, 14 | Matched companion pins to the HW bank |
| `16..23` | Software PWM | 18, 19, 20, 21, 22, 25, 26, 27 | Lower-frequency bank |

## Hardware PWM (8 channels)

Both RP2040 and RP2350 provide PWM slices with A/B outputs. The firmware uses **one channel per slice**, so each of the 8 hardware channels has a **fully independent frequency**.

- GPIO: 1, 3, 5, 7, 9, 11, 13, 15 (channel B of slices 0-7)
- Target operating range: about 10 Hz to 1 MHz
- Pulse counting: every PWM wrap triggers an interrupt on Core 1 that increments the channel's 32-bit pulse counter

The frequency is configured by selecting a clock divider (`clkdiv`) and a wrap value (`TOP`). The firmware searches all valid `(TOP, clkdiv)` combinations to find the best match.

```
f_pwm = f_sys / (clkdiv * (TOP + 1))
```

## PIO PWM (8 channels)

The PIO PWM bank fills the gap between hardware PWM and timer-driven software PWM.

- GPIO: 0, 2, 4, 6, 8, 10, 12, 14
- Target operating range: about 10 Hz to 100 kHz
- Positioning: good accuracy, deterministic timing, and a pinout that mirrors the hardware bank

This bank is intended for channels that need better timing quality than software PWM but do not need to consume the MCU hardware PWM slices.

## Software PWM (8 channels)

The software PWM channels are driven by a periodic hardware timer interrupt on Core 1.

- GPIO: 18, 19, 20, 21, 22, 25, 26, 27
- Target operating range: about 1 Hz to 1 kHz
- Timer tick: 10 µs (100 kHz interrupt rate)
- Resolution at 1 kHz: 100 levels (1% duty steps)

The timer ISR maintains a per-channel counter. When the counter reaches the period value, the GPIO is toggled and the counter resets; the pulse counter is incremented at the same time.

State updates (`set_freq`, `set_duty`) use `save_and_disable_interrupts()` / `restore_interrupts()` to prevent the ISR from reading partially-updated struct fields.

## Control Interface

`control.c` presents a single, uniform view of all 24 channels, regardless of backend.

Each channel has three properties:

| Property | Type | Description |
|----------|------|-------------|
| `freq` | `float` | Frequency in Hz. `0` means the channel is off. |
| `duty` | `float` | Duty cycle from 0.0 to 1.0. |
| `pulse_count` | `uint32_t` | Number of PWM periods completed. Wraps to 0 on overflow. Read-only. |

Initialization values (power-up / reset):
- `freq = 0` (off)
- `duty = 0.5` (50%)
- `pulse_count = 0`

**Important:** `pulse_count` is read-only from all external interfaces. The `stop` command is the only way to clear it.

## Command Transports

### USB CDC (text)

The Pico appears as a USB serial device (CDC). A text-based command parser reads lines and calls the control API. Responses are printed back to the same interface. `tud_task()` is serviced in the Core 0 main loop.

### I2C Slave (binary)

GPIO 16/17 are configured as I2C0 slave at 7-bit address `0x40`. An interrupt-driven handler on Core 0 responds to read-only status queries from an I2C master. The protocol is byte-oriented and binary (little-endian).

Both transports expose the same data: device type, version, channel count, and per-channel properties.

## Startup Flow

1. **Core 0**: `set_sys_clock_khz(150000, true)` — overclock to 150 MHz
2. **Core 0**: `stdio_init_all()` — initialize USB CDC
3. **Core 0**: `control_init()` — init cached values + command queue
4. **Core 0**: `pwm_core_launch()` — start Core 1
5. **Core 1**: `hw_pwm_init()` — init PWM slices + wrap IRQ on Core 1
6. **Core 1**: `pio_pwm_init()` — init PIO-backed PWM channels
7. **Core 1**: `sw_pwm_init()` — init SW PWM GPIO + timer IRQ on Core 1
8. **Core 1**: set `pwm_ready = true`, enter main loop (`control_process_pending` + `__wfi`)
9. **Core 0**: wait for `pwm_core_is_ready()`
10. **Core 0**: `cdc_cmd_init()` / `i2c_slave_init()` — start command interfaces
11. **Core 0**: print help, enter main loop (`cdc_cmd_poll` + `i2c_slave_poll`)

## Performance Notes

- Core 1's main loop sleeps with `__wfi()` between timer interrupts, consuming near-zero power when idle.
- The 100 kHz SW PWM timer wakes Core 1 every 10 µs. Between wakeups, the queue is drained and the core sleeps again.
- HW PWM wrap interrupts add extra wakeups proportional to PWM frequency. At 100 kHz on all 8 channels, that's 800k interrupts/sec — manageable at 150 MHz.
- USB CDC and I2C on Core 0 are never preempted by PWM ISRs, ensuring reliable communication.
- `pulse_count` reads use volatile 32-bit loads, which are atomic on ARM Cortex-M0+.
