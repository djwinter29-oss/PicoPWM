#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
VENV_DIR="${VENV_DIR:-$REPO_ROOT/host/python/.venv}"
BASE_PYTHON="${PYTHON:-python3}"

case "${1:-}" in
    -h|--help)
        cat <<'EOF'
Usage: tools/host/run-python.sh

Run the Python web host at http://127.0.0.1:5000/.
Creates a virtualenv and installs the host when needed.
Set PYTHON to select Python 3.10+, or VENV_DIR to select the environment.
The host talks to the board over USB HID. Optional: PICOPWM_SERIAL,
PICOPWM_DEVICE_PATH, PICOPWM_HOST, and PICOPWM_PORT.
EOF
        exit 0
        ;;
    "")
        ;;
    *)
        echo "Unknown argument: $1; use --help." >&2
        exit 2
        ;;
esac

if [ ! -x "$VENV_DIR/bin/python" ]; then
    if ! command -v "$BASE_PYTHON" >/dev/null 2>&1; then
        echo "Python interpreter not found: $BASE_PYTHON" >&2
        exit 1
    fi
    "$BASE_PYTHON" -c 'import sys; raise SystemExit(sys.version_info < (3, 10))' || {
        echo "PicoPWM requires Python 3.10 or newer." >&2
        exit 1
    }
    "$BASE_PYTHON" -m venv "$VENV_DIR"
fi

PYTHON_EXE="$VENV_DIR/bin/python"
if ! "$PYTHON_EXE" -c 'import flask, hid' >/dev/null 2>&1; then
    "$PYTHON_EXE" -m pip install "$REPO_ROOT/host/python"
fi

PYTHONPATH="$REPO_ROOT/host/python${PYTHONPATH:+:$PYTHONPATH}" exec "$PYTHON_EXE" -m picopwm.app
