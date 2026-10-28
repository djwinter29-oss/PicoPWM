#!/usr/bin/env sh
set -eu

BOARD="${PICO_BOARD:-pico}"
ELF_PATH=""
FLASH_SIZE="${PICO_FLASH_SIZE_BYTES:-}"
SKIP_BUILD=0
OPENOCD="${OPENOCD:-openocd}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --board)
            BOARD="$2"
            shift 2
            ;;
        --elf)
            ELF_PATH="$2"
            shift 2
            ;;
        --flash-size)
            FLASH_SIZE="$2"
            shift 2
            ;;
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

case "$BOARD" in
    pico)
        OPENOCD_TARGET=rp2040
        DEFAULT_FLASH_SIZE=2097152
        ;;
    pico2)
        OPENOCD_TARGET=rp2350
        DEFAULT_FLASH_SIZE=4194304
        ;;
    *)
        echo "Unsupported board '$BOARD'. Use 'pico' or 'pico2'." >&2
        exit 1
        ;;
esac

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR_PATH="$REPO_ROOT/build/$BOARD"

if [ -z "$FLASH_SIZE" ]; then
    FLASH_SIZE="$DEFAULT_FLASH_SIZE"
fi

case "$FLASH_SIZE" in
    ''|*[!0-9]*)
        echo "Flash size must be a positive decimal byte count: $FLASH_SIZE" >&2
        exit 1
        ;;
esac

if [ "$FLASH_SIZE" -le 0 ] || [ "$((FLASH_SIZE % 4096))" -ne 0 ]; then
    echo "Flash size must be a positive multiple of 4096 bytes: $FLASH_SIZE" >&2
    exit 1
fi

if [ "$SKIP_BUILD" -eq 0 ]; then
    "$SCRIPT_DIR/build.sh" --board "$BOARD"
fi

if [ -z "$ELF_PATH" ]; then
    ELF_PATH="$BUILD_DIR_PATH/pico_pwm.elf"
fi

if [ ! -f "$ELF_PATH" ]; then
    echo "ELF file not found: $ELF_PATH" >&2
    exit 1
fi

if ! command -v "$OPENOCD" >/dev/null 2>&1; then
    echo "OpenOCD not found: $OPENOCD. Install OpenOCD and connect a Pico Debug Probe over SWD." >&2
    exit 1
fi

"$OPENOCD" \
    -f interface/cmsis-dap.cfg \
    -c "set FLASHSIZE $FLASH_SIZE" \
    -f "target/$OPENOCD_TARGET.cfg" \
    -c "adapter speed 5000" \
    -c "program \"$ELF_PATH\" verify reset exit"