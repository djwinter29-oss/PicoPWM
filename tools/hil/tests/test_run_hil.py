"""Hardware-free regression tests for CDC HIL safety gates, measurements and reporting."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
import run_hil as hil


class FakeConsole:
    def __init__(self, roles=("gen", "mon", "mon"), fail_monitor=False):
        self.roles = roles
        self.states = {}
        self.commands = []
        self.fail_monitor = fail_monitor

    def command(self, command):
        self.commands.append(command)
        if command == "info":
            return "PicoPWM\nclock=125000000 Hz target=met"
        if command == "version":
            return "0.1.1"
        if command == "config":
            return ("running=clk=125MHz i2c=0x40 A:hw/%s B:pio/%s C:sw/%s" % self.roles
                    + "\ntarget=clk=125MHz i2c=0x40 A:hw/mon B:pio/mon C:sw/mon")
        if command == "stop":
            self.states.clear()
            return "OK all channels stopped"
        fields = command.split()
        if fields[0] == "set":
            channel, freq, duty = map(int, fields[1:])
            self.states[channel % 8] = (freq, duty)
            return "OK"
        if fields[0] == "get":
            channel = int(fields[1])
            if self.fail_monitor and self.roles[channel // 8] == "mon":
                raise hil.HilError("Disconnected monitor")
            freq, duty = self.states.get(channel % 8, (0, 0))
            return f"CH{channel}: freq={freq} Hz, duty={duty}%, enabled={'yes' if freq else 'no'}"
        raise AssertionError(command)


def arguments():
    return argparse.Namespace(
        frequencies=[100], frequency_tolerance=5.0, duty_tolerance=5.0,
        settle_timeout=0.01, command_timeout=0.01, board="pico", port="fake",
        firmware=Path("fixture.uf2"),
    )


class HilTests(unittest.TestCase):
    @unittest.skipIf(os.name == "nt", "POSIX launcher; direct runner is tested on Windows")
    def test_launcher_creates_environment_installs_once_and_forwards_arguments(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = root / "python"
            fake.write_text(f"#!{sys.executable}\n" + """
import json, os, pathlib, shutil, sys
root = pathlib.Path(os.environ["FAKE_ROOT"])
args = sys.argv[1:]
with (root / "calls").open("a") as log:
    log.write(json.dumps(args) + "\\n")
if args[:2] == ["-m", "venv"]:
    target = pathlib.Path(args[2]) / "bin/python"
    target.parent.mkdir(parents=True)
    shutil.copyfile(__file__, target)
    target.chmod(0o755)
elif args[:3] == ["-m", "pip", "install"]:
    (root / "installed").touch()
elif args[:1] == ["-c"] and "import serial" in args[1]:
    sys.exit(0 if (root / "installed").exists() else 1)
elif args[:1] != ["-c"]:
    sys.exit(7)
""")
            fake.chmod(0o755)
            env = dict(os.environ, PYTHON=str(fake), VENV_DIR=str(root / "venv"), FAKE_ROOT=str(root))
            launcher = hil.ROOT / "tools/host/run-hil.sh"
            arguments = ["--port", "port with spaces", "--confirm-fixture"]
            for _ in range(2):
                result = subprocess.run(["sh", str(launcher), *arguments], cwd=root, env=env)
                self.assertEqual(result.returncode, 7)
            calls = [json.loads(line) for line in (root / "calls").read_text().splitlines()]
            self.assertEqual(sum(call[:2] == ["-m", "venv"] for call in calls), 1)
            self.assertEqual(sum(call[:3] == ["-m", "pip", "install"] for call in calls), 1)
            self.assertEqual(calls[-1], ["-m", "run_hil", *arguments])

    def test_each_generator_bank_and_final_stop(self):
        for generator in range(3):
            roles = ["mon"] * 3
            roles[generator] = "gen"
            console = FakeConsole(tuple(roles))
            results = []
            hil.exercise(console, arguments(), results)
            self.assertEqual(console.commands[-1], "stop")
            self.assertEqual(len([r for r in results if r[0].startswith("PWM-01")]), 24)
            self.assertEqual(len([r for r in results if r[0].startswith("LOAD-01")]), 8)
            self.assertTrue(all(status == "PASS" for _, status, _ in results))
            self.assertFalse(any(c.startswith(("config ", "reboot")) for c in console.commands))

    def test_unsafe_roles_never_drive_or_stop(self):
        for roles in [("mon",) * 3, ("gen", "gen", "mon")]:
            console = FakeConsole(roles)
            with self.assertRaises(hil.HilError):
                hil.exercise(console, arguments(), [])
            self.assertFalse(any(c.startswith(("set ", "stop")) for c in console.commands))

    def test_measurement_failure_still_stops(self):
        console = FakeConsole(fail_monitor=True)
        with self.assertRaises(hil.HilError):
            hil.exercise(console, arguments(), [])
        self.assertEqual(console.commands[-1], "stop")

    def test_monitor_mismatch_times_out(self):
        console = FakeConsole()
        with self.assertRaisesRegex(hil.HilError, "settling deadline"):
            hil.wait_monitor(console, 8, (100, 50), arguments())

    def test_channel_response_must_match_requested_channel(self):
        console = FakeConsole()
        with patch.object(console, "command", return_value="CH9: freq=100 Hz, duty=50%, enabled=yes"):
            with self.assertRaises(hil.HilError):
                hil.read_state(console, 8)

    def test_prompt_framing_and_timeout(self):
        class Port:
            in_waiting = 0

            def __init__(self, data):
                self.data = bytearray(data)

            def read(self, size):
                value = bytes(self.data[:size])
                del self.data[:size]
                return value

        console = hil.Console(Port(b"\x1b[2KCH0: freq=100 Hz, duty=50%, enabled=yes\r\npico> "), 1)
        self.assertIn("freq=100", console.read_prompt())
        with self.assertRaises(hil.HilError):
            hil.Console(Port(b"incomplete"), 0.001).read_prompt()

    def test_record_replaces_old_content_and_marks_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            record = Path(directory) / "latest.md"
            record.write_text("old results")
            with patch.object(hil, "RECORD", record):
                hil.write_record(arguments(), [("RUN", "FAIL", "missing | wire")], ["> stop"], "now", "abc")
            text = record.read_text()
            self.assertNotIn("old results", text)
            self.assertIn("Outcome: FAIL", text)
            self.assertIn("missing \\| wire", text)
            self.assertIn("Not Executed", text)
            self.assertEqual(list(Path(directory).iterdir()), [record])

    def test_main_with_fake_serial_writes_passing_record(self):
        class Port:
            def __init__(self, *args, **kwargs):
                self.data = bytearray()
                self.console = FakeConsole()

            def __enter__(self):
                return self

            def __exit__(self, *args):
                return False

            @property
            def in_waiting(self):
                return len(self.data)

            def reset_input_buffer(self):
                self.data.clear()

            def write(self, data):
                command = data.decode().strip()
                response = self.console.command(command) if command else ""
                self.data.extend(f"{command}\r\n{response}\r\npico> ".encode())

            def read(self, size):
                data = bytes(self.data[:size])
                del self.data[:size]
                return data

        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "fixture.uf2"
            artifact.write_bytes(b"unit-test artifact, not firmware")
            record = Path(directory) / "latest.md"
            with patch.object(hil, "RECORD", record), patch.dict("sys.modules", {"serial": SimpleNamespace(Serial=Port)}):
                status = hil.main([
                    "--port", "fake", "--board", "pico", "--firmware", str(artifact),
                    "--confirm-fixture", "--frequencies", "100",
                ])
            self.assertEqual(status, 0)
            self.assertIn("PASS (automated subset only)", record.read_text())
            self.assertIn(hil.hashlib.sha256(artifact.read_bytes()).hexdigest(), record.read_text())

    def test_missing_artifact_writes_failure_record(self):
        with tempfile.TemporaryDirectory() as directory:
            record = Path(directory) / "latest.md"
            with patch.object(hil, "RECORD", record):
                status = hil.main([
                    "--port", "fake", "--board", "pico", "--firmware", str(Path(directory) / "missing.uf2"),
                    "--confirm-fixture",
                ])
            self.assertEqual(status, 1)
            self.assertIn("Outcome: FAIL", record.read_text())


if __name__ == "__main__":
    unittest.main()
