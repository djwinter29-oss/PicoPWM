# PWM Driver Detailed Design

This document describes the detailed design of the `pwm` subsystem in the current PicoPWM firmware.

The focus of this page is:

- subsystem responsibilities
- cross-core separation
- mailbox and snapshot behavior
- state machines
- API call sequences
- backend selection and routing boundaries

This document is implementation-oriented and follows the current firmware under `firmware/src/driver/pwm/`.

## Scope

The `pwm` subsystem provides one logical channel service. The default
profile currently exposes 24 channels, but backend ownership and direction are
resolved from the startup bank configuration and `config/board.c`.
`pwm_driver_channel_gpio()` exposes only this immutable allocation through the
driver boundary, allowing the Core 0 device facade to build HID pin reports
without accessing backend runtime state or duplicating the map in hosts.

It is responsible for:

- owning all PWM backend state on Core 1
- translating logical channel IDs to backend-local channel IDs
- accepting command-path frequency and duty updates from Core 0
- publishing realized channel state for readback
- hiding backend-specific timing details from higher layers

It is not responsible for:

- parsing CLI commands
- I2C transport framing
- user-facing validation policy beyond basic driver-side checks

## Design Goals

The subsystem is designed around the following goals:

1. Keep communication and user interface handling on Core 0.
2. Keep PWM timing engines, IRQs, and backend state on Core 1.
3. Expose one logical channel model to higher layers.
4. Separate command submission from backend implementation details.
5. Publish realized state rather than maintaining a second shadow model in a separate control layer.

### Runtime Service Telemetry

Core 1 measures wall time around mailbox processing and monitor refresh, excluding
its WFE/timeout wait. Approximately once per second it publishes one aligned
32-bit service-load value in basis points (0-10000), read through
`pwm_driver_service_load()` and the Core 0 `device` facade. This is a workload
proxy, not CPU utilization: IRQ work outside measured service passes is excluded,
and lockout or preemption during a pass can affect the result. It does not change
backend ownership or IRQ/timer placement. The initial value is zero until the
first measurement window completes.

The idle wait retains one deadline across early SDK wakeups, unless the mailbox
has a pending command. Alarm setup and unrelated events must not trigger extra
monitor refresh passes or repeatedly postpone the intended 1 ms polling wait.
This keeps commands responsive without turning idle monitoring into a hot loop.

## Source Layout

The current implementation is split as follows:

| File | Responsibility |
| ------ | ---------------- |
| `firmware/src/device.h` | Shared Core 0 device API surface used by CDC, HID, and I2C |
| `firmware/src/device.c` | Shared device info, channel reads, and channel write helpers above the Core 1 mailbox boundary |
| `firmware/src/app/i2c/i2c_control_map.h` | I2C register map definitions and protocol helpers |
| `firmware/src/app/i2c/i2c_control_map.c` | I2C register encode/decode and deferred write translation into `device` |
| `firmware/src/driver/pwm/pwm_driver.h` | Public wrapper API, bank/role types, board GPIO structs and channel capacity constants |
| `firmware/src/driver/pwm/pwm_driver.c` | Core 1 launch, mailbox loop, bank roles, channel lookups, frequency limits, board GPIO validation, shared snapshot |
| `firmware/src/platform/boot_config.[ch]` | Persisted clock profile, bank roles and I2C address (CRC-protected flash record) |
| `firmware/src/driver/pwm/hardware/hardware_generator.c` | Hardware PWM generator backend |
| `firmware/src/driver/pwm/hardware/hardware_monitor.c` | Hardware PWM monitor backend |
| `firmware/src/driver/pwm/pio/pio_generator.c` | PIO generator backend |
| `firmware/src/driver/pwm/pio/pio_generator.pio` | PIO assembly program used by the PIO generator backend |
| `firmware/src/driver/pwm/pio/pio_monitor.c` | PIO monitor backend |
| `firmware/src/driver/pwm/pio/pio_monitor.pio` | PIO assembly program used by the PIO monitor backend |
| `firmware/src/driver/pwm/software/software_generator.c` | Software PWM generator backend |
| `firmware/src/driver/pwm/software/software_monitor.c` | Software PWM monitor backend |

## External Interface

The transport-facing control/status layer is:

```c
const char *device_name(void);
const char *device_firmware_version(void);
uint8_t device_channel_count(void);
bool device_get_channel(uint channel, pwm_state_t *state);
pwm_driver_result_t device_set_channel(uint channel, uint32_t freq_hz, uint8_t duty);
pwm_driver_result_t device_restore_defaults(void);
```

Below that, `pwm` owns the internal cross-core mailbox boundary used by `device`:

```c
bool pwm_driver_init(const pwm_role_t roles[PWM_BANK_COUNT]);
bool pwm_driver_is_ready(void);
```

The internal Core 0 control layer uses the following mailbox API:

```c
pwm_driver_result_t pwm_driver_set(uint channel, uint32_t freq_hz, uint8_t duty);
bool pwm_driver_get(uint channel, pwm_state_t *state);
pwm_driver_result_t pwm_driver_restore_defaults(void);
```

The shared state type is:

```c
typedef struct {
    uint32_t freq_hz;
    uint8_t duty;
} pwm_state_t;
```

### API Intent

Architecturally, `pwm_driver_set()` is an internal command-ingress API.

- Top-level command paths such as the CDC CLI, HID action handler, and I2C register map should enter through `device`.
- The I2C write path should continue to defer out of the ISR before it reaches this mailbox API.
- It is not intended as a general-purpose helper for arbitrary internal call sites.
- Core 1 callers must not use this API; it returns `PWM_DRIVER_RESULT_UNAVAILABLE` outside the Core 0 command path.
- Public write callers first pass through the shared Core 0 serialization lock before they compete for mailbox admission.
- Core 0 waits for Core 1 to finish the backend apply step before returning.
- The wrapper returns `PWM_DRIVER_RESULT_INVALID` for invalid channel or unsupported frequency requests.
- The wrapper returns `PWM_DRIVER_RESULT_UNAVAILABLE` if Core 1 is not ready yet.
- The wrapper returns `PWM_DRIVER_RESULT_TIMEOUT` when the command is still unclaimed at the apply deadline. Core 0 cancels that command and it is not applied. If Core 1 has already claimed it, Core 0 keeps waiting and returns the published result. A stalled Core 1 heartbeat during that wait reboots the board.
- The wrapper returns `PWM_DRIVER_RESULT_APPLY_FAILED` if Core 1 accepts the command but the backend rejects it.
- `pwm_driver_restore_defaults()` has no mailbox command of its own: it calls `pwm_driver_set(ch, 0, 0)` for every generator channel, continues after individual failures, and returns the first failure after all generator channels have been attempted.

## Current Default Mapping

Every bank maps to the same fixed logical channel range. Startup configuration
selects its backend family and role:

| Logical Channel | Backend when locked as generator | Backend when locked as monitor |
| ----------------- | --------- | ----------------------- |
| `0..7` (Bank A) | HW generator | HW monitor |
| `8..15` (Bank B) | PIO generator | PIO monitor |
| `16..23` (Bank C) | SW generator only | SW monitor only |

The physical mapping and slice assignments are documented in
[Hardware Setup](../../guides/hardware-setup.md). Startup bank configuration is documented in
[PWM Driver Configuration](configuration.md).

## Internal Separation

The subsystem has four internal layers.

### 1. Transport Layer

Owned by the CDC CLI, HID, and I2C transport modules.

Responsibilities:

- parse transport-specific requests
- format transport-specific responses
- avoid backend access and multicore synchronization details

For I2C specifically, the transport layer includes an ISR-facing slave module and a register-map helper that translates binary registers into shared control-layer operations.

### 2. Shared Control Layer

Owned by `firmware/src/device.c`.

Responsibilities:

- expose one shared Core 0 control/status API to all three transports
- centralize device identity and version reporting
- centralize logical channel reads and writes above `pwm`
- avoid maintaining a second shadow copy of realized channel state

This layer reads realized state from `pwm_driver_get()` and forwards writes through the internal `pwm` mailbox API.

It does not own I2C register numbers or binary payload layout. Those protocol details live in `firmware/src/app/i2c/i2c_control_map.c`.

### 3. Wrapper Layer

Owned by `pwm_driver.c`.

Responsibilities:

- serialize public write entry points on Core 0
- own mailbox admission and reply waiting for the internal `pwm_driver_set()` and `pwm_driver_restore_defaults()` APIs

This layer does not know backend-specific register or timing details.

This layer is the architectural boundary between the shared Core 0 control plane and backend-specific code.

### 4. Backend Layer

Owned by the selected profile's generator or monitor modules:

- `hardware/hardware_generator.c` and `hardware/hardware_monitor.c`
- `pio/pio_generator.c` and `pio/pio_monitor.c`
- `software/software_generator.c` and `software/software_monitor.c`

Responsibilities:

- own backend timing configuration
- own backend-local state
- manage backend IRQ or timer behavior
- publish realized state after successful changes

### 5. Hardware/Runtime Layer

Owned by the Pico SDK and the MCU peripherals.

Resources used:

- PWM slices
- GPIO edge IRQs for hardware and software monitor channels
- PIO programs, state machines, and IRQs
- one repeating timer on a private Core 1 alarm pool for software PWM, armed only while a channel is active
- multicore event signaling

## Core Ownership Model

### Core 0

Core 0 owns:

- USB CDC transport
- I2C slave transport
- CLI command parsing and formatting
- shared control/status translation
- status formatting and reporting

Core 0 reads channel state through `pwm_driver_get()`.

### Core 1

Core 1 owns:

- all PWM backend initialization
- all backend-local timing configuration
- all PWM-related IRQ handling
- the software PWM timer callback
- shared snapshot publication

Core 0 must not call backend driver functions directly.

## High-Level State Machine

The `pwm` wrapper has a small lifecycle state machine.

```mermaid
stateDiagram-v2
    [*] --> Reset
    Reset --> LaunchRequested: pwm_driver_init(roles)
    LaunchRequested --> InitializingCore1: multicore_launch_core1()
    InitializingCore1 --> BackendInit: Core1 entry
    BackendInit --> Ready: hw init + pio init + sw init complete
    Ready --> Ready: process mailbox command
    Ready --> Ready: publish state snapshot
    Ready --> Ready: wait for event
```

### State Descriptions

| State | Meaning |
| ------ | --------- |
| `Reset` | Static memory and snapshot defaults only |
| `LaunchRequested` | Core 0 requested Core 1 startup |
| `BackendInit` | Core 1 is initializing all backends |
| `Ready` | Mailbox processing and PWM service active |

## Per-Channel Operational State Machine

Each logical channel behaves like this from the wrapper point of view.

```mermaid
stateDiagram-v2
    [*] --> Disabled
    Disabled --> ApplyRequested: set_freq(ch, f>0, duty)
    ApplyRequested --> Enabled: backend apply ok
    ApplyRequested --> Disabled: backend apply failed
    Enabled --> UpdateRequested: set_freq(ch, f>0 or duty change)
    UpdateRequested --> Enabled: backend apply ok
    UpdateRequested --> Enabled: backend apply failed, previous config retained
    Enabled --> Disabled: set_freq(ch, 0, duty)
    Disabled --> Disabled: read state
```

This is a logical model. Backend-specific internal state differs by driver.

## API Sequence Diagrams

### `pwm_driver_init()`

```mermaid
sequenceDiagram
    participant C0 as Core 0 main
    participant WR as pwm_driver.c
    participant C1 as Core 1

    C0->>WR: pwm_driver_init(roles)
    WR->>WR: init pending mailbox slot
    WR->>WR: init snapshot defaults for the startup bank roles
    WR->>C1: multicore_launch_core1(core_main)
    C1->>C1: gpio_set_irq_callback(pwm_driver_monitor_gpio_irq)
    C1->>WR: pwm_ready = true
    C1->>C1: mailbox loop (1 ms wait with monitors, 100 ms without)
```

All selected backends initialize during startup from the supplied configuration.

### `pwm_driver_init()` startup configuration

```mermaid
sequenceDiagram
    participant Main as Core 0 main
    participant WR as pwm_driver.c
    participant C1 as Core 1
    participant BE as selected backends

    Main->>WR: pwm_driver_init(roles)
    WR->>WR: record and validate bank roles
    WR->>C1: multicore_launch_core1(core_main)
    C1->>BE: initialize selected backends
    BE-->>C1: success/failure
    C1->>WR: publish ready state
```

### `device_set_channel()` to `pwm_driver_set()`

```mermaid
sequenceDiagram
    participant CLI as CDC CLI / command layer
    participant CTL as device
    participant WR as pwm_driver.c
    participant C1 as Core 1 mailbox loop
    participant BE as selected backend

    CLI->>CTL: device_set_channel(ch, freq, duty)
    CTL->>WR: pwm_driver_set(ch, freq, duty)
    WR->>WR: admit one in-flight mailbox command
    WR->>C1: __sev()
    C1->>C1: claim active command
    C1->>BE: backend_set(local_ch, freq, duty)
    BE->>WR: publish realized state
    C1->>WR: publish apply result
    WR-->>CTL: result
    CTL-->>CLI: result
```

### `device_restore_defaults()` to `pwm_driver_restore_defaults()`

```mermaid
sequenceDiagram
    participant CLI as CDC CLI / command layer
    participant CTL as device
    participant WR as pwm_driver.c
    participant C1 as Core 1 mailbox loop

    CLI->>CTL: device_restore_defaults()
    CTL->>WR: pwm_driver_restore_defaults()
    loop each generator channel
        WR->>C1: pwm_driver_set(ch, 0, 0)
        C1->>WR: publish apply result
    end
    WR-->>CTL: result
    CTL-->>CLI: result
```

### Read Path

```mermaid
sequenceDiagram
    participant UI as CLI/I2C status path
    participant CTL as device
    participant WR as pwm_driver.c
    participant SS as shared snapshot

    UI->>CTL: device_get_channel(ch, &state)
    CTL->>WR: pwm_driver_get(ch, &state)
    WR->>SS: read snapshot under the cache lock
    SS-->>WR: coherent state struct
    WR-->>CTL: pwm_state_t
    CTL-->>UI: coherent state struct
```

The coherent higher-layer read boundary is `device_get_channel()`, which forwards one channel snapshot from `pwm_driver_get()`.

## Mailbox Command Structure

The wrapper uses one single-slot mailbox record containing:

- operation kind
- logical channel for set-channel commands
- requested frequency for set-channel commands
- requested duty for set-channel commands

Only one command is in flight at a time: the Core 0 serialization lock admits one writer, and Core 0 waits until
its command is applied, so no `BUSY` result is produced here (`PWM_DRIVER_RESULT_BUSY` remains the I2C queued-write status).

The slot is one struct of three flags plus the command, all guarded by the reply critical section:

- `pending`: published by Core 0, not yet claimed. Core 1 clears it when it claims the command.
- `done`: Core 1 finished applying the command.
- `ok`: the backend result, valid when `done` is set.

`!pending && !done` therefore means Core 1 is applying the command. Core 0 may cancel only while `pending` is still set,
at the apply deadline.

The mailbox carries control mutations and their result status. It does not
carry real-time PWM edges or monitor samples; those remain local to Core 1 and
are exposed through the realized-state snapshot.

### Responsibilities of `pwm_driver.c`

`pwm_driver.c` performs the following functions.

1. Profile-table channel routing.
2. Profile-based mapping from logical channel number to backend and backend-local index.
3. Core 1 launch and ready-state control.
4. Mailbox state transitions for the one-slot Core 0/Core 1 command exchange.
5. Reply publication for the active Core 0 command.
6. Shared-state publication.
7. Readback through the locked snapshot.
8. Backend initialization on Core 1 from the startup bank configuration.

### Logical Routing

Routing is resolved on demand: `channel / 8` selects the fixed module
(A=hardware, B=pio, C=software), the startup role selects its generator or
monitor variant, `channel % 8` is the module-local index, and each module reads
its GPIOs from its `board.c` const. There is no stored table. The
default profile currently exposes 24 channels, but higher layers must not
infer backend ownership from a channel-number range.

Each descriptor owns:

- backend initialization and channel operations
- generator `set()` and restore callbacks when the channel is output-capable
- monitor read callbacks when the channel is input-capable

### Shared Snapshot Design

Each logical channel has one published `pwm_state_t` (`freq_hz`, `duty`) in a cache guarded by one
`critical_section_t` (a hardware spinlock plus local interrupt mask, so it is safe across cores and from the
Core 1 timer ISR). Writers (`pwm_driver_store_applied_state()`) and the reader (`pwm_driver_get()`) hold it only for one
struct copy, so the pair is never torn and no retry loop exists. The cache is zero (`0 Hz`, `0%`) until a backend
publishes.

Core 1 refreshes monitor channels about every 1 ms (skipped entirely when no bank is a monitor; the loop then wakes on submit or every 100 ms) but republishes a channel only when its sample changed, so a
monitor whose value is stable costs one compare per channel.

A monitor with no valid sample (signal lost, or nothing captured yet) is published as `0 Hz, 0%`, never as the last
valid sample. `0 Hz` therefore means "no periodic signal" for a monitor; the duty of a static input is reported by
backends that can sample the level.

## Backend Designs

The backend-specific design pages each cover both generation and monitoring
for one hardware family:

- [Hardware PWM](hardware.md)
- [PIO PWM](pio.md)
- [Software PWM](software.md)

Use those pages for resource constraints, timing and measurement strategies,
and backend-specific tradeoffs. This page focuses on the shared driver,
ownership boundary, routing, and realized-state publication.

This keeps the callback from observing partially updated configuration.

## Snapshot Publication Model

Backends publish state through one helper function in `pwm_driver.c`.

### `pwm_driver_store_applied_state()`

Used after a successful apply of:

- new frequency
- new duty
- disable request

Published fields:

- realized `freq_hz`
- realized `duty`

## Readback Semantics

The architectural intent is that the `pwm` snapshot is the single source of truth for logical channel state.

Important readback properties:

- values are realized backend values, not merely requested values; frequency is rounded to the nearest achievable
  one and duty is derived from the integer step counts that program the output, so callers read both back
- a static request (zero frequency, `0%` or `100%`) is published as `0 Hz` with a realized duty of `0` or `100`, for
  every generator backend
- `stop` disables generator output
- callers should prefer a single state-struct read when they need coherent multi-field data

## Failure and Boundary Conditions

### Common Wrapper-Level Rejections

The wrapper rejects:

- invalid channel index
- unsupported mailbox operation kind

### Backend-Level Rejections

Hardware PWM may reject invalid channel numbers.

PIO PWM may reject frequencies that cannot be represented by the program timing search.

Software PWM owns its own maximum supported frequency policy and resolves static outputs to the
driven realized level in published state.

Launch-time cache seeding uses the same shared logical default as backend restore-defaults:

- `freq_hz = 0`
- `duty = 0`

### Mailbox Pressure

The wrapper accepts one in-flight mailbox command at a time.

The public write layer also keeps a small Core 0 mutex around the write entry points so only one mailbox submission path can compete for the single-slot command record at a time.

After admission, the caller waits synchronously for the Core 1 reply, up to the apply timeout. If the command is still unclaimed when that deadline expires, Core 0 cancels it and returns `PWM_DRIVER_RESULT_TIMEOUT`; the command will not be applied. If Core 1 has already claimed it, Core 0 keeps waiting and returns the published result. A stalled Core 1 heartbeat during that wait reboots the board.

Restore-defaults is a loop of per-channel stop requests through the normal setter path.

## Design Constraints and Assumptions

The current design assumes:

1. Core 0 is the normal producer of `pwm_driver_set()` and `pwm_driver_restore_defaults()` requests through `device`.
2. Backend drivers remain Core 1 implementation details.
3. CDC CLI and I2C both use the same shared Core 0 control facade.
4. The shared snapshot is the only read model exposed upward.

I2C writes should continue to defer out of ISR context before they enter `device` and the internal `pwm` mailbox boundary.

## Startup Bank Configuration

`pwm_driver.c` records the per-bank role passed to
`pwm_driver_init()` before Core 1 starts and resolves channels from them. There is no
bank-lock mailbox operation; one firmware image reaches all 8 bank-role
combinations by selecting roles in the startup configuration.

## Summary

The `pwm` subsystem is a logical-channel wrapper around three different PWM timing engines.

Its main value is not the backends themselves, but the separation it enforces:

- Core 0 handles user-facing command and status paths.
- Core 1 owns timing, hardware state, and backend implementation details.
- One logical API and one realized-state snapshot are shared upward.

This allows higher layers to think in terms of logical channels while still using the best available timing engine per channel class.
