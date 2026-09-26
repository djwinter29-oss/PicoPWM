#!/usr/bin/env sh
set -eu

PYTHON_EXE="${PYTHON_EXE:-python3}"
PYTHON_FILES=$(find . -type f -name '*.py' -not -path './.git/*' -not -path '*/__pycache__/*' -print)

if [ -z "$PYTHON_FILES" ]; then
    echo "No Python files found."
    exit 0
fi

# shellcheck disable=SC2086
"$PYTHON_EXE" -m py_compile $PYTHON_FILES
