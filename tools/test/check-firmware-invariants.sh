#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)

PIO_FILE="$REPO_ROOT/firmware/src/driver/pwm/pio/pio_generator.pio"
DRIVER_FILE="$REPO_ROOT/firmware/src/driver/pwm/pwm_driver.c"
CONFIG_FILE="$REPO_ROOT/firmware/src/platform/boot_config.c"
SLAVE_FILE="$REPO_ROOT/firmware/src/driver/i2c/i2c_slave.c"
SLAVE_HW_FILE="$REPO_ROOT/firmware/src/driver/i2c/i2c_slave_hw.c"
SW_GEN_FILE="$REPO_ROOT/firmware/src/driver/pwm/software/software_generator.c"
SW_MON_FILE="$REPO_ROOT/firmware/src/driver/pwm/software/software_monitor.c"
if grep -q 'gpio_set_irq_enabled' "$SW_MON_FILE" ||
   ! grep -q 'alarm_pool_create_with_unused_hardware_alarm' "$SW_MON_FILE"; then
    echo "software_monitor.c: monitoring must use a private timer, not per-edge GPIO IRQs" >&2
    exit 1
fi
PIO_MON_FILE="$REPO_ROOT/firmware/src/driver/pwm/pio/pio_monitor.pio"
PIO_MON_C_FILE="$REPO_ROOT/firmware/src/driver/pwm/pio/pio_monitor.c"

if ! grep -q 'sm_config_set_in_pins(&config, pin)' "$PIO_MON_FILE" ||
   ! grep -q 'sm_config_set_jmp_pin(&config, pin)' "$PIO_MON_FILE"; then
    echo "pio_monitor.pio: WAIT PIN and JMP PIN must both select the monitored GPIO" >&2
    exit 1
fi

# SDK restart clears internal state, not the PC left in the terminal capture loop.
if ! grep -q 'pio_sm_exec(ctx->pio, ctx->sm, pio_encode_jmp(pio_mon_program_offsets' "$PIO_MON_C_FILE"; then
    echo "pio_monitor.c: each capture must jump back to its loaded program entry point" >&2
    exit 1
fi

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

# The default alarm pool belongs to Core 0; the software tick must run on a Core 1 pool.
if grep -Eq "(^|[^_a-z])add_repeating_timer_us" "$SW_GEN_FILE" || ! grep -q "alarm_pool_create_with_unused_hardware_alarm" "$SW_GEN_FILE"; then
    echo "software_generator.c: the tick timer must use a private alarm pool created on Core 1" >&2
    exit 1
fi

if ! grep -q "multicore_lockout_victim_init" "$DRIVER_FILE"; then
    echo "pwm_driver.c: Core 1 must arm the flash lockout victim before reporting ready" >&2
    exit 1
fi

if ! grep -q "multicore_lockout_start_blocking" "$CONFIG_FILE"; then
    echo "boot_config.c: config save must lock out Core 1 around flash programming" >&2
    exit 1
fi

# Flash helpers in SDK 2.3.0 do not mask IRQs. Interrupts must be off around erase and program.
awk '
    /save_and_disable_interrupts/ { disabled = 1 }
    /multicore_lockout_start_blocking/ {
        if (!disabled) {
            print "boot_config.c: lock out Core 1 only while interrupts are disabled" > "/dev/stderr"
            exit 1
        }
        locked = 1
    }
    /flash_range_erase/ {
        if (!disabled || !locked) {
            print "boot_config.c: erase flash only with interrupts disabled and Core 1 locked out" > "/dev/stderr"
            exit 1
        }
        erased = 1
    }
    /flash_range_program/ {
        if (!erased) {
            print "boot_config.c: program flash only after erase, with interrupts still disabled" > "/dev/stderr"
            exit 1
        }
        programmed = 1
    }
    /multicore_lockout_end_blocking/ {
        if (!programmed) {
            print "boot_config.c: end the Core 1 lockout after flash programming" > "/dev/stderr"
            exit 1
        }
    }
    /restore_interrupts/ {
        if (!programmed) {
            print "boot_config.c: restore interrupts after flash programming" > "/dev/stderr"
            exit 1
        }
        restored = 1
    }
    END {
        if (!restored) {
            print "boot_config.c: config save must disable interrupts around flash programming" > "/dev/stderr"
            exit 1
        }
    }
' "$CONFIG_FILE"

if ! grep -q "clr_tx_abrt" "$SLAVE_HW_FILE"; then
    echo "i2c_slave_hw.c: events must clear TX_ABRT so a master NACK releases the FIFO" >&2
    exit 1
fi

if ! grep -q "I2C_IC_INTR_MASK_M_TX_ABRT_BITS" "$SLAVE_HW_FILE"; then
    echo "i2c_slave_hw.c: TX_ABRT must be unmasked so a master NACK enters the ISR" >&2
    exit 1
fi

if grep -q "hw->clr_intr" "$SLAVE_HW_FILE"; then
    echo "i2c_slave_hw.c: clr_intr drops STOP_DET and RD_REQ that arrive after the status sample" >&2
    exit 1
fi

if ! grep -q "rxflr" "$SLAVE_HW_FILE" || ! grep -q "i2c_slave_hw_rx_pop" "$SLAVE_FILE"; then
    echo "i2c_slave.c: ISR must drain every queued RX byte before handling STOP" >&2
    exit 1
fi

if ! grep "I2C_IC_INTR_MASK_M_TX_EMPTY_BITS" "$SLAVE_HW_FILE" | grep -q "enabled"; then
    echo "i2c_slave_hw.c: TX_EMPTY mask must follow the enabled flag" >&2
    exit 1
fi

if grep "I2C_IC_INTR_MASK_M_TX_EMPTY_BITS" "$SLAVE_HW_FILE" | grep -v "enabled" | grep -q .; then
    echo "i2c_slave_hw.c: TX_EMPTY must stay masked unless a read still has bytes to send" >&2
    exit 1
fi

MAIN_FILE="$REPO_ROOT/firmware/src/main.c"
PIO_C_FILE="$REPO_ROOT/firmware/src/driver/pwm/pio/pio_generator.c"
HW_MATH_FILE="$REPO_ROOT/firmware/src/driver/pwm/hardware/hardware_math.h"
CDC_FILE="$REPO_ROOT/firmware/src/app/usb/usb_cdc.c"

if ! grep -q "pwm_wait_hook_active" "$DRIVER_FILE"; then
    echo "pwm_driver.c: a wait hook that submits another command must return BUSY" >&2
    exit 1
fi

if ! grep -q "gen_publish_state(channel, 0u, 0u)" "$PIO_C_FILE"; then
    echo "pio_generator.c: a failed FIFO push must publish the stopped pin state" >&2
    exit 1
fi

if ! grep -q "pwm_driver_set_wait_hook" "$MAIN_FILE"; then
    echo "main.c: Core 0 must register a PWM-apply wait hook so USB keeps moving" >&2
    exit 1
fi

if ! grep -q "pwm_pio_fifo_put_action" "$PIO_C_FILE"; then
    echo "pio_generator.c: TX writes must use the bounded FIFO action" >&2
    exit 1
fi

if ! grep -q "div_x16 <= PWM_HW_MAX_DIV_X16" "$HW_MATH_FILE" || grep -q "PWM_HW_DIV_SEARCH_WINDOW" "$HW_MATH_FILE"; then
    echo "hardware_math.h: timing search must walk every divider through PWM_HW_MAX_DIV_X16" >&2
    exit 1
fi

if ! grep -q "usb_cdc_rx_count < USB_CDC_RX_QUEUE_SIZE" "$CDC_FILE"; then
    echo "usb_cdc.c: a full local RX queue must stop reading so the host is NAKed" >&2
    exit 1
fi
