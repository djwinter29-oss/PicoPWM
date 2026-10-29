# Software PWM Design

The software generator produces PWM through one shared repeating timer and a
per-channel scheduler state. The timer runs on a private Core 1 alarm pool and is
armed only while a channel is active. Source files:

- `firmware/src/driver/pwm/software/software_generator.c`
- `firmware/src/driver/pwm/software/software_generator.h`

## Resource Constraints

Software generation does not require a fixed PWM slice or PIO state machine.
A profile may assign valid, uniquely owned GPIOs to software channels, but the
practical channel count is limited by Core 1 CPU time, timer callback work,
and interrupt load.

The current scheduler tick is `10 us`, producing a `100 kHz` base tick. The
recommended generator range is about `1 Hz .. 1 kHz`; period and duty are
quantized to integer scheduler ticks.

## Operation

The shared callback visits active software channels, advances their counters,
and updates GPIO outputs. Static output cases bypass the scheduled path:

- `freq_hz = 0`, `duty = 100`: static high
- `freq_hz = 0`, other duty: static low
- nonzero frequency with `duty = 0` or `100`: static output level

```mermaid
flowchart TD
    Request[Logical frequency and duty] --> Static{Zero frequency or endpoint duty?}
    Static -- Yes --> Level[Drive static GPIO level]
    Static -- No --> Period[Calculate integer period ticks]
    Period --> Window[Calculate duty window]
    Window --> Timer[Shared timer callback updates channels]
    Level --> Publish[Publish realized state]
    Timer --> Publish
```

Software generation is intentionally the simple, low-frequency backend. Use
hardware PWM or PIO for tighter timing accuracy or higher-frequency output.

## Monitor Design

The software monitor observes GPIO edges using interrupts and timestamps. It
derives an approximate frequency from the period between edges and duty from
the high-time portion of that period.

```mermaid
sequenceDiagram
    participant Signal as PWM input
    participant IRQ as GPIO edge interrupt
    participant Monitor as Software monitor
    participant State as Published channel state

    Signal->>IRQ: Rising and falling edges
    IRQ->>Monitor: Capture timestamps
    Monitor->>Monitor: Derive period and high width
    Monitor->>State: Publish approximate frequency and duty
```

Software monitoring does not require a fixed PWM slice or PIO state machine.
Its practical limits are the shared scheduler, CPU time, interrupt load, and
timestamp granularity. The current working range is about `1 Hz .. 1 kHz`,
subject to the selected profile's limits. Use hardware or PIO monitoring for
higher-frequency or higher-accuracy measurements.

If no transition is observed for more than one second, the monitor reports a
static input as `0 Hz` with duty `0%` or `100%`, based on the sampled level.
