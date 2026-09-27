#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)

PIO_FILE="$REPO_ROOT/firmware/src/pwmdriver/generator/pio_generator.pio"
DRIVER_FILE="$REPO_ROOT/firmware/src/pwmdriver/pwm_driver.c"
CONFIG_FILE="$REPO_ROOT/firmware/src/pwmdriver/pwm_driver_config.c"
SLAVE_FILE="$REPO_ROOT/firmware/src/i2c/i2c_slave.c"

# The low path must not side-set. A side-set there makes every duty a one-tick pulse.
awk '
    $1 == "noset:" { in_noset = 1; next }
    in_noset && $1 == "nop" {
        if ($0 ~ /side/) {
            print "pio_generator.pio: noset nop must not side-set" > "/dev/stderr"
            exit 1
        }
        found_nop = 1
    }
    in_noset && $1 == "skip:" { in_noset = 0 }
    END {
        if (!found_nop) {
            print "pio_generator.pio: missing noset nop" > "/dev/stderr"
            exit 1
        }
    }
' "$PIO_FILE"

if ! grep -q "jmp skip        side 1" "$PIO_FILE"; then
    echo "pio_generator.pio: missing sticky-high side-set" >&2
    exit 1
fi

if ! grep -q "multicore_lockout_victim_init" "$DRIVER_FILE"; then
    echo "pwm_driver.c: Core 1 must call multicore_lockout_victim_init" >&2
    exit 1
fi

if ! grep -q "multicore_lockout_start_blocking" "$CONFIG_FILE"; then
    echo "pwm_driver_config.c: config save must lock out Core 1 around flash programming" >&2
    exit 1
fi

# Flash helpers in SDK 2.3.0 do not mask IRQs. Interrupts must be off around erase and program.
awk '
    /save_and_disable_interrupts/ { disabled = 1 }
    /multicore_lockout_start_blocking/ {
        if (!disabled) {
            print "pwm_driver_config.c: lock out Core 1 only while interrupts are disabled" > "/dev/stderr"
            exit 1
        }
        locked = 1
    }
    /flash_range_erase/ {
        if (!disabled || !locked) {
            print "pwm_driver_config.c: erase flash only with interrupts disabled and Core 1 locked out" > "/dev/stderr"
            exit 1
        }
        erased = 1
    }
    /flash_range_program/ {
        if (!erased) {
            print "pwm_driver_config.c: program flash only after erase, with interrupts still disabled" > "/dev/stderr"
            exit 1
        }
        programmed = 1
    }
    /multicore_lockout_end_blocking/ {
        if (!programmed) {
            print "pwm_driver_config.c: end the Core 1 lockout after flash programming" > "/dev/stderr"
            exit 1
        }
    }
    /restore_interrupts/ {
        if (!programmed) {
            print "pwm_driver_config.c: restore interrupts after flash programming" > "/dev/stderr"
            exit 1
        }
        restored = 1
    }
    END {
        if (!restored) {
            print "pwm_driver_config.c: config save must disable interrupts around flash programming" > "/dev/stderr"
            exit 1
        }
    }
' "$CONFIG_FILE"

if ! grep -q "clr_tx_abrt" "$SLAVE_FILE"; then
    echo "i2c_slave.c: ISR must clear TX_ABRT so a master NACK releases the FIFO" >&2
    exit 1
fi

if ! grep -q "i2c_control_map_is_status_select" "$SLAVE_FILE"; then
    echo "i2c_slave.c: a one-byte write to a longer command must select status instead of queueing" >&2
    exit 1
fi

if ! grep -q "i2c_irq_gate_should_refresh" "$SLAVE_FILE"; then
    echo "i2c_slave.c: a new read must refresh a consumed status buffer" >&2
    exit 1
fi

if ! grep -q "i2c_irq_gate_on_read_request" "$SLAVE_FILE"; then
    echo "i2c_slave.c: ISR must call i2c_irq_gate_on_read_request" >&2
    exit 1
fi

if ! grep -q "i2c_irq_gate_on_tx_empty" "$SLAVE_FILE"; then
    echo "i2c_slave.c: ISR must call i2c_irq_gate_on_tx_empty" >&2
    exit 1
fi

if ! grep "I2C_IC_INTR_MASK_M_TX_EMPTY_BITS" "$SLAVE_FILE" | grep -q "tx_empty_unmasked"; then
    echo "i2c_slave.c: TX_EMPTY mask must follow tx_empty_unmasked" >&2
    exit 1
fi

if grep "I2C_IC_INTR_MASK_M_TX_EMPTY_BITS" "$SLAVE_FILE" | grep -v "tx_empty_unmasked" | grep -q .; then
    echo "i2c_slave.c: TX_EMPTY must stay masked unless tx_empty_unmasked" >&2
    exit 1
fi
