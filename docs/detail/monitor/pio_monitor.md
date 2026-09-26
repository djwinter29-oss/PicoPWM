# PIO PWM Monitor Design

The PIO monitor measures PWM input using one state machine per configured
channel and DMA-backed high/low snapshots. Source files:

- `firmware/src/pwmdriver/monitor/pio_monitor.c`
- `firmware/src/pwmdriver/monitor/pio_monitor.h`
- `firmware/src/pwmdriver/monitor/pio_monitor.pio`

## Constraints

PIO monitoring consumes finite PIO state machines and requires GPIO routing
compatible with the monitor program. The default profile uses eight channels,
four per PIO block. A custom profile must avoid resource and GPIO conflicts.

The monitor keeps only the latest two-word high/low pair. Intermediate periods
can be discarded, reads use a best-effort stability check, and the finite DMA
transfer eventually exhausts in the current implementation. This backend is
intended for higher-performance monitoring than software polling, but it still
reports approximate frequency and duty.

## Measurement Flow

```mermaid
flowchart TD
    Input[PWM input] --> SM[PIO state machine counts high and low]
    SM --> DMA[DMA latest high/low pair]
    DMA --> Decode[Core 1 decode and stability check]
    Decode --> Snapshot[Publish realized state]
```

Unstable samples use the documented sentinel state. Static-level fallback is
applied after the configured inactivity timeout while DMA remains active.
