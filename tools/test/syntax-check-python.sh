#!/usr/bin/env sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

set -eu

PYTHON_EXE="${PYTHON_EXE:-python3}"
PYTHON_FILES=$(find . -type f -name '*.py' \
    -not -path './.git/*' \
    -not -path '*/__pycache__/*' \
    -not -path './firmware/third_party/pico-sdk/*' \
    -not -path './firmware/third_party/embedded-cli/*' \
    -not -path './build/*' \
    -print)

if [ -z "$PYTHON_FILES" ]; then
    echo "No Python files found."
    exit 0
fi

# shellcheck disable=SC2086
"$PYTHON_EXE" -m py_compile $PYTHON_FILES
