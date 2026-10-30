# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""Check firmware build generator selection without downloading or compiling."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


BUILD_SCRIPT = Path(__file__).resolve().parents[1] / "firmware" / "build.sh"


class FirmwareBuildTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        script = self.root / "tools" / "firmware" / "build.sh"
        script.parent.mkdir(parents=True)
        shutil.copyfile(BUILD_SCRIPT, script)
        self.script = script
        self.command("git", "exit 0")
        self.command("cmake", 'printf "generator=%s\\n" "${CMAKE_GENERATOR:-default}"\n'
                     'printf "<%s>" "$@"\nprintf "\\n"')
        (self.bin / "dirname").symlink_to(shutil.which("dirname"))
        self.env = dict(os.environ, PATH=str(self.bin))
        for name in ("CMAKE_GENERATOR", "PICO_BOARD", "PICO_PWM_FIRMWARE_VERSION"):
            self.env.pop(name, None)

    def command(self, name, body):
        path = self.bin / name
        path.write_text("#!/bin/sh\n" + body + "\n", encoding="ascii")
        path.chmod(0o755)

    def build(self, *args):
        return subprocess.run(
            ["/bin/sh", str(self.script), *args],
            env=self.env, text=True, capture_output=True, check=True,
        ).stdout

    def test_fresh_build_prefers_ninja(self):
        self.command("ninja", "exit 0")
        output = self.build("--board", "pico2", "--firmware-version", "1.2.3")
        self.assertIn("generator=Ninja", output)
        self.assertIn("<-DPICO_BOARD=pico2>", output)
        self.assertIn("<-DPICO_PWM_FIRMWARE_VERSION=1.2.3>", output)
        self.assertIn(f"<--build><{self.root}/build/pico2><--parallel>", output)

    def test_without_ninja_uses_cmake_default(self):
        self.assertIn("generator=default", self.build())

    def test_explicit_generator_wins(self):
        self.command("ninja", "exit 0")
        self.env["CMAKE_GENERATOR"] = "Unix Makefiles"
        self.assertIn("generator=Unix Makefiles", self.build())

    def test_existing_cache_leaves_selection_to_cmake(self):
        self.command("ninja", "exit 0")
        build_dir = self.root / "build" / "pico"
        build_dir.mkdir(parents=True)
        cache = build_dir / "CMakeCache.txt"
        cache.write_text("CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n", encoding="ascii")
        self.assertIn("generator=default", self.build())
        self.assertEqual(cache.read_text(), "CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n")


if __name__ == "__main__":
    unittest.main()
