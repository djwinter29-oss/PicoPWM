#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)

PIO_FILE="$REPO_ROOT/firmware/src/pwmdriver/generator/pio_generator.pio"
DRIVER_FILE="$REPO_ROOT/firmware/src/pwmdriver/pwm_driver.c"
CONFIG_FILE="$REPO_ROOT/firmware/src/pwmdriver/pwm_driver_config.c"

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
