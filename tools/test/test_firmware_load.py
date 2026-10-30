# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""Check probe-based firmware loading without a board or OpenOCD installation."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


LOAD_SCRIPT = Path(__file__).resolve().parents[1] / "firmware" / "load.sh"


class FirmwareLoadTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.script = self.root / "tools" / "firmware" / "load.sh"
        self.script.parent.mkdir(parents=True)
        shutil.copyfile(LOAD_SCRIPT, self.script)
        (self.bin / "dirname").symlink_to(shutil.which("dirname"))
        self.calls = self.root / "openocd-args"
        self.openocd = self.bin / "openocd"
        self.openocd.write_text(
            "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$OPENOCD_CALLS\"\n",
            encoding="ascii",
        )
        self.openocd.chmod(0o755)
        self.env = {
            **os.environ,
            "PATH": str(self.bin),
            "OPENOCD": str(self.openocd),
            "OPENOCD_CALLS": str(self.calls),
        }
        self.env.pop("PICO_BOARD", None)

    def run_loader(self, *args):
        return subprocess.run(
            ["/bin/sh", str(self.script), *args],
            env=self.env,
            text=True,
            capture_output=True,
        )

    def test_programs_board_elf_through_debug_probe(self):
        for board, target, flash_size in (
            ("pico", "rp2040", "2097152"),
            ("pico2", "rp2350", "4194304"),
        ):
            with self.subTest(board=board), tempfile.TemporaryDirectory() as temporary:
                elf = Path(temporary) / "pico_pwm.elf"
                elf.touch()
                result = self.run_loader("--board", board, "--skip-build", "--elf", str(elf))

                self.assertEqual(result.returncode, 0, result.stderr)
                args = self.calls.read_text(encoding="ascii").splitlines()
                self.assertEqual(args[args.index("-f") + 1], "interface/cmsis-dap.cfg")
                self.assertEqual(args[args.index("-f", args.index("-f") + 1) + 1], f"target/{target}.cfg")
                self.assertIn(f"set FLASHSIZE {flash_size}", args)
                self.assertIn(f'program "{elf}" verify reset exit', args)

    def test_accepts_flash_size_override(self):
        with tempfile.TemporaryDirectory() as temporary:
            elf = Path(temporary) / "pico_pwm.elf"
            elf.touch()
            result = self.run_loader(
                "--skip-build", "--elf", str(elf), "--flash-size", "8388608"
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("set FLASHSIZE 8388608", self.calls.read_text(encoding="ascii"))

    def test_rejects_invalid_flash_size(self):
        with tempfile.TemporaryDirectory() as temporary:
            elf = Path(temporary) / "pico_pwm.elf"
            elf.touch()
            result = self.run_loader(
                "--skip-build", "--elf", str(elf), "--flash-size", "not-a-size"
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("positive decimal byte count", result.stderr)
            self.assertFalse(self.calls.exists())


if __name__ == "__main__":
    unittest.main()