#!/usr/bin/env python3
"""Run non-persistent CDC loopback checks and atomically replace the latest HIL record."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import math
import os
from pathlib import Path
import platform
import re
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[3]
RECORD = ROOT / "docs/tests/records/latest.md"
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
PROMPT = re.compile(r"(?:^|[\r\n])pico> $")
STATE = re.compile(r"CH(\d+): freq=(\d+) Hz, duty=(\d+)%, enabled=(yes|no)")
RUNNING = re.compile(
    r"^running=clk=(\d+)MHz i2c=0x([0-9A-Fa-f]{2}) "
    r"A:hw/(gen|mon) B:pio/(gen|mon) C:sw/(gen|mon)\r?$", re.MULTILINE
)


class HilError(RuntimeError):
    """A failed protocol, fixture or measurement check."""


class Console:
    """Prompt-framed CDC exchange; transport timeouts abort rather than risk desynchronization."""

    def __init__(self, port, timeout: float):
        self.port = port
        self.timeout = timeout
        self.transcript: list[str] = []

    def read_prompt(self) -> str:
        deadline = time.monotonic() + self.timeout
        data = bytearray()
        while time.monotonic() < deadline:
            data.extend(self.port.read(self.port.in_waiting or 1))
            text = ANSI.sub("", data.decode("utf-8", errors="replace"))
            if PROMPT.search(text):
                return text[:text.rfind("pico> ")].strip()
            if len(data) > 65536:
                raise HilError("CDC response exceeded 64 KiB")
        raise HilError("Timed out waiting for the CDC prompt; command outcome is unknown")

    def synchronize(self) -> None:
        self.port.reset_input_buffer()
        self.transcript.append("[initial prompt synchronization]")
        self.port.write(b"\r")
        self.transcript.append(self.read_prompt())

    def command(self, command: str) -> str:
        self.transcript.append(f"> {command}")
        self.port.write((command + "\r").encode("ascii"))
        response = self.read_prompt()
        self.transcript.append(response)
        if re.search(r"^ERR\b", response, re.MULTILINE):
            raise HilError(f"{command}: {response}")
        return response


def read_state(console: Console, channel: int) -> tuple[int, int]:
    match = STATE.search(console.command(f"get {channel}"))
    if not match or int(match[1]) != channel:
        raise HilError(f"Unrecognized channel {channel} response")
    return int(match[2]), int(match[3])


def require_ok(console: Console, command: str) -> None:
    if not re.search(r"^OK\b", console.command(command), re.MULTILINE):
        raise HilError(f"{command}: missing OK acknowledgement")


def within_limits(actual: tuple[int, int], expected: tuple[int, int], args) -> bool:
    return (
        abs(actual[0] - expected[0]) <= max(1.0, expected[0] * args.frequency_tolerance / 100)
        and abs(actual[1] - expected[1]) <= args.duty_tolerance
    )


def wait_monitor(console: Console, channel: int, expected: tuple[int, int], args) -> tuple[int, int]:
    deadline = time.monotonic() + args.settle_timeout
    while True:
        actual = read_state(console, channel)
        if within_limits(actual, expected, args):
            return actual
        if time.monotonic() >= deadline:
            raise HilError(f"CH{channel}: expected {expected}, observed {actual}; settling deadline exceeded")
        time.sleep(0.1)


def exercise(console: Console, args, results: list[tuple[str, str, str]]) -> None:
    """Validate roles before touching output, then run isolated and eight-channel loopbacks."""
    info = console.command("info")
    if not re.search(r"^PicoPWM\r?$", info, re.MULTILINE):
        raise HilError("Connected CDC device did not identify as PicoPWM")
    version = console.command("version")
    config = console.command("config")
    match = RUNNING.search(config)
    if not match:
        raise HilError("Cannot parse running bank configuration")
    roles = list(match.groups()[2:])
    if roles.count("gen") != 1 or roles.count("mon") != 2:
        raise HilError("Shared fixture requires exactly one running generator bank and two monitor banks")
    generator = roles.index("gen")
    monitors = [bank for bank, role in enumerate(roles) if role == "mon"]
    results.append(("BOOT-02 / CDC", "PASS", f"{info}; version response: {version}; {match[0]}"))
    # Once fixture roles are verified, always attempt stop, including after a failed command.
    try:
        require_ok(console, "stop")
        for local in range(8):
            for frequency in args.frequencies:
                for duty in (25, 50, 75):
                    source = generator * 8 + local
                    require_ok(console, f"set {source} {frequency} {duty}")
                    realized = read_state(console, source)
                    if realized[0] == 0 or not within_limits(realized, (frequency, duty), args):
                        raise HilError(f"CH{source}: unexpected realized state {realized}")
                    observations = [
                        (bank * 8 + local, wait_monitor(console, bank * 8 + local, realized, args))
                        for bank in monitors
                    ]
                    results.append((
                        f"PWM-01 / CH{source} / {frequency} Hz / {duty}%",
                        "PASS", f"realized={realized}; monitors={observations}",
                    ))
            require_ok(console, f"set {source} 0 0")

        for local in range(8):
            require_ok(console, f"set {generator * 8 + local} {args.frequencies[0]} 50")
        for local in range(8):
            source = generator * 8 + local
            realized = read_state(console, source)
            if not within_limits(realized, (args.frequencies[0], 50), args):
                raise HilError(f"CH{source}: unexpected loaded state {realized}")
            observations = [
                (bank * 8 + local, wait_monitor(console, bank * 8 + local, realized, args))
                for bank in monitors
            ]
            results.append((f"LOAD-01 / partial / CH{source}", "PASS",
                            f"Eight active outputs; realized={realized}; monitors={observations}"))
        require_ok(console, "stop")
        for local in range(8):
            source = generator * 8 + local
            if read_state(console, source) != (0, 0):
                raise HilError(f"CH{source}: stop did not publish 0 Hz / 0%")
        results.append(("PWM-04 / readback only", "PASS", "All generator channels report 0 Hz / 0% after stop"))
    finally:
        try:
            require_ok(console, "stop")
        except (HilError, OSError) as error:
            results.append(("CLEANUP", "FAIL", f"Stop could not be confirmed: {error}. Disconnect board power."))
            raise


def write_record(args, results, transcript, started: str, artifact_hash: str) -> None:
    """Replace the fixed report only after the complete new report has been written."""
    def cell(text) -> str:
        return str(text).replace("|", "\\|").replace("\r", "").replace("\n", "<br>")

    failed = any(status == "FAIL" for _, status, _ in results)
    lines = [
        "# Latest CDC HIL Test Record", "",
        f"- Started (UTC): {started}",
        f"- Finished (UTC): {datetime.now(timezone.utc).isoformat()}",
        f"- Outcome: {'FAIL' if failed else 'PASS (automated subset only)'}",
        f"- Board (operator supplied): {cell(args.board)}",
        f"- Host: {cell(platform.platform())}; Python {platform.python_version()}",
        f"- CDC port: {cell(args.port)}",
        f"- Artifact (operator supplied; not verified against device): {cell(args.firmware)}",
        f"- Artifact SHA-256: {artifact_hash}",
        "- Fixture: eight separate A/B/C junctions, one 1 kOhm resistor per GPIO branch; operator confirmed.",
        f"- Requested frequencies: {args.frequencies} Hz; duty cycles: 25%, 50%, 75%.",
        f"- Frequency tolerance: {args.frequency_tolerance}% (minimum 1 Hz); duty tolerance: "
        f"{args.duty_tolerance} percentage points.",
        f"- Per-monitor settling timeout: {args.settle_timeout} s; CDC command timeout: {args.command_timeout} s.",
        "- No flash writes, bank-role changes or reboot commands were issued.",
        "- This record replaces the previous latest.md; copy it to a dated filename to retain history.",
        "", "## Results", "", "| Case | Status | Evidence |", "| --- | --- | --- |",
    ]
    lines.extend(f"| {cell(case)} | {status} | {cell(detail)} |" for case, status, detail in results)
    lines.extend([
        "", "## Not Executed / Not Established", "",
        "- Other generator-bank passes: rerun after manual role configuration and safe fixture reconnection.",
        "- Absolute accuracy, jitter, physical static/stop levels and Core 1 tick ownership: need instruments/debugger.",
        "- Full LOAD-01: no timed 60-second run or simultaneous HID/I2C traffic.",
        "- BOOT-01, CFG-01..05, PWM-02/03/05, MON-01/02, IO-01..04, SAVE-01 and REC-01/02: "
        "not fully executed by this runner.",
        "- Loopback agreement is not independent hardware qualification. Unreached checks after a failure are not passes.",
        "", "## CDC Transcript", "", "````text",
        "\n".join(transcript).replace("````", "[four backticks]"), "````", "",
    ])
    RECORD.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=RECORD.parent,
                                         prefix=".latest-", suffix=".tmp", delete=False) as report:
            temporary = Path(report.name)
            report.write("\n".join(lines))
        os.replace(temporary, RECORD)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def positive(value: str) -> float:
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero")
    return number


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="CDC port, e.g. /dev/ttyACM0 or COM5")
    parser.add_argument("--board", required=True, choices=("pico", "pico2"))
    parser.add_argument("--firmware", required=True, type=Path, help="Exact flashed artifact, for SHA-256 recording")
    parser.add_argument("--confirm-fixture", required=True, action="store_true",
                        help="Confirm isolated three-bank fixture and permission to drive and stop its outputs")
    parser.add_argument("--frequencies", nargs="+", type=int, default=[100, 500, 1000])
    parser.add_argument("--frequency-tolerance", type=positive, default=5.0, help="Percent, default 5")
    parser.add_argument("--duty-tolerance", type=positive, default=5.0, help="Percentage points, default 5")
    parser.add_argument("--settle-timeout", type=positive, default=3.0)
    parser.add_argument("--command-timeout", type=positive, default=3.0)
    args = parser.parse_args(argv)
    if any(f < 100 or f > 1000 for f in args.frequencies):
        parser.error("shared-fixture runner frequencies must be 100..1000 Hz")
    started = datetime.now(timezone.utc).isoformat()
    results: list[tuple[str, str, str]] = []
    console = None
    artifact_hash = "unavailable"
    try:
        artifact_hash = hashlib.sha256(args.firmware.read_bytes()).hexdigest()
        try:
            import serial
        except ImportError as error:
            raise HilError("Install the HIL project: python -m pip install --editable tools/hil") from error
        with serial.Serial(args.port, 115200, timeout=0.1, write_timeout=args.command_timeout) as port:
            console = Console(port, args.command_timeout)
            console.synchronize()
            exercise(console, args, results)
            results.append(("RUN", "PASS", "CDC automated subset completed; final stop acknowledged"))
    except (HilError, OSError, KeyboardInterrupt) as error:
        results.append(("RUN", "FAIL", str(error) or "Interrupted by operator"))
        print(f"HIL failed: {error}. If output state is uncertain, disconnect board power.", file=sys.stderr)
    finally:
        if not any(case == "RUN" for case, _, _ in results):
            results.append(("RUN", "FAIL", "Run terminated before completion"))
        write_record(args, results, console.transcript if console else [], started, artifact_hash)
        print(f"Record: {RECORD}")
    return 1 if any(status == "FAIL" for _, status, _ in results) else 0


if __name__ == "__main__":
    sys.exit(main())
