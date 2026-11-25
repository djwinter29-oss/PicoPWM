# PWM Driver Design

The driver is the multicore boundary: Core 0 sends requests; Core 1 owns
backend state and timing.

## Boundary and Ownership

Transport/control code must not call backends directly. I2C commands leave
interrupt context before entering shared control. Startup roles fix ownership:
A uses hardware channels 0-7, B uses PIO 8-15, and C uses software 16-23.

## Applying Changes

One channel command at a time crosses the mailbox to Core 1. Core 0 waits
for the backend result. It cancels commands unclaimed after one second;
claimed commands must finish rather than return an uncertain cancellation.
Invalid or unsupported requests are rejected before or during application,
and failures reach the requesting transport.

The mailbox carries commands, not PWM edges or monitor samples.
Implementation: [`pwm_driver.c`](../../../firmware/src/driver/pwm/pwm_driver.c);
interface: [`pwm_driver.h`](../../../firmware/src/driver/pwm/pwm_driver.h).

## Realized-State Snapshot

Core 1 publishes the latest configured or measured state per channel.
Core 0 reads that snapshot, not a separate requested-state cache.
Synchronization keeps frequency and duty coherent.

Generator readings include timing quantization; monitor readings are the
latest sample, not waveform history. The format cannot always distinguish
an unavailable monitor sample from static low.

## Stop Behavior

Stop disables generator output without changing saved bank roles.

Backend timing: [hardware](backend-hardware.md), [PIO](backend-pio.md),
[software](backend-software.md).
[Operating ranges](../../guides/configuration.md#pwm-operating-ranges).
