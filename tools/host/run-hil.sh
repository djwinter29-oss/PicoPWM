#!/usr/bin/env sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
VENV_DIR="${VENV_DIR:-$REPO_ROOT/build/hil-venv}"
BASE_PYTHON="${PYTHON:-python3}"

case "${1:-}" in
    -h|--help)
        cat <<'EOF'
Usage: tools/host/run-hil.sh --port PORT --board pico|pico2 --firmware PATH --confirm-fixture [runner options]

Run the CDC loopback HIL tests and replace docs/tests/records/latest.md.
Creates a virtualenv under build/hil-venv and installs the HIL project in editable mode.
Set PYTHON to select Python 3.10+, or VENV_DIR to select the environment.
Requires the documented three-bank fixture and one generator/two monitor banks.
Drives and stops outputs; never saves configuration or reboots the board.
Use picopwm-hil --help in the environment for additional runner options.
EOF
        exit 0
        ;;
esac

if [ ! -x "$VENV_DIR/bin/python" ]; then
    if ! command -v "$BASE_PYTHON" >/dev/null 2>&1; then
        echo "Python interpreter not found: $BASE_PYTHON" >&2
        exit 1
    fi
    "$BASE_PYTHON" -c 'import sys; raise SystemExit(sys.version_info < (3, 10))' || {
        echo "PicoPWM HIL requires Python 3.10 or newer." >&2
        exit 1
    }
    "$BASE_PYTHON" -m venv "$VENV_DIR"
fi

PYTHON_EXE="$VENV_DIR/bin/python"
if ! "$PYTHON_EXE" -c 'from importlib.metadata import version; from pathlib import Path; import serial, run_hil, sys; version("picopwm-hil"); v = tuple(map(int, version("pyserial").split("."))); raise SystemExit(not ((3, 5) <= v < (4,)) or Path(run_hil.__file__).resolve() != Path(sys.argv[1]).resolve())' "$REPO_ROOT/tools/hil/src/run_hil.py" >/dev/null 2>&1; then
    "$PYTHON_EXE" -m pip install --editable "$REPO_ROOT/tools/hil"
fi

exec "$PYTHON_EXE" -m run_hil "$@"
