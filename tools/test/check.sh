#!/usr/bin/env sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

set -eu

# Compatibility entry point; test-firmware-c.sh owns the host C test workflow.
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
exec "$SCRIPT_DIR/test-firmware-c.sh" "$@"
