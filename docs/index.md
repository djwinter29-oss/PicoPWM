# PicoPWM Documentation

Welcome to the PicoPWM documentation. Use the links below to navigate.

- [Architecture](architecture.md) — Target system layout, firmware roles, backend split, and interface model.
- [PWM Driver Design](detail/pwm_driver_design.md) — Detailed `pwmdriver` design, state machines, API sequences, and backend behavior.
- [Control Protocol](protocol.md) — USB CDC serial protocol for read/write control and I2C slave protocol for read-only channel status.
- [Hardware Notes](hardware.md) — Generator/monitor shared pinout, GPIO allocation, and wiring notes.
- [Internal API](api.md) — Current source-level API for the control and PWM implementation modules.
- [Build & Flash](build.md) — Build flow, firmware variants, and flash instructions.

For a high-level overview, see the [README.md](../README.md) in the project root.
