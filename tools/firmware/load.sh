#!/usr/bin/env sh
set -eu

BOARD="${PICO_BOARD:-pico}"
UF2_PATH=""
MOUNT_PATH="${PICO_MOUNT_PATH:-}"
SKIP_BUILD=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --board)
            BOARD="$2"
            shift 2
            ;;
        --uf2)
            UF2_PATH="$2"
            shift 2
            ;;
        --mount-path)
            MOUNT_PATH="$2"
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
    pico|pico2) ;;
    *)
        echo "Unsupported board '$BOARD'. Use 'pico' or 'pico2'." >&2
        exit 1
        ;;
esac

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
BUILD_DIR_PATH="$REPO_ROOT/build/$BOARD"

if [ "$SKIP_BUILD" -eq 0 ]; then
    "$SCRIPT_DIR/build.sh" --board "$BOARD"
fi

if [ -z "$UF2_PATH" ]; then
    UF2_PATH="$BUILD_DIR_PATH/pico_pwm.uf2"
fi

if [ ! -f "$UF2_PATH" ]; then
    echo "UF2 file not found: $UF2_PATH" >&2
    exit 1
fi

if command -v picotool >/dev/null 2>&1; then
    picotool load "$UF2_PATH" -f
    picotool reboot
    exit 0
fi

if [ -z "$MOUNT_PATH" ]; then
    for candidate in \
        "/media/$USER/RPI-RP2" \
        "/run/media/$USER/RPI-RP2" \
        "/Volumes/RPI-RP2"
    do
        if [ -d "$candidate" ]; then
            MOUNT_PATH="$candidate"
            break
        fi
    done
fi

if [ -z "$MOUNT_PATH" ] || [ ! -d "$MOUNT_PATH" ]; then
    echo "Could not find the RPI-RP2 mount point. Use --mount-path or install picotool." >&2
    exit 1
fi

cp "$UF2_PATH" "$MOUNT_PATH/pico_pwm.uf2"