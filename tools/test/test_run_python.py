"""Hardware-free launcher checks: python3 tools/test/test_run_python.py."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RunPythonTests(unittest.TestCase):
    def test_existing_venv_with_and_without_pip(self):
        for pip_available in ("0", "1"):
            with self.subTest(pip_available=pip_available), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / "bin").mkdir()
                interpreter = root / "bin/python"
                interpreter.write_text(
                    '#!/bin/sh\n'
                    'printf "%s\\n" "$*" >> "$CALLS"\n'
                    'if [ "$*" = "-m pip --version" ]; then exit "$PIP_AVAILABLE"; fi\n'
                    'exit 0\n'
                )
                interpreter.chmod(0o755)
                udevadm = root / "bin/udevadm"
                udevadm.write_text('#!/bin/sh\nprintf "/devices/not-picopwm\\n"\n')
                udevadm.chmod(0o755)
                base = root / "base-python"
                base.write_text('#!/bin/sh\nprintf "base %s\\n" "$*" >> "$CALLS"\nexit 0\n')
                base.chmod(0o755)
                calls = root / "calls"
                subprocess.run(
                    ["sh", str(ROOT / "tools/host/run-python.sh")], check=True,
                    env={**os.environ, "VENV_DIR": str(root), "PYTHON": str(base),
                         "CALLS": str(calls), "PIP_AVAILABLE": pip_available,
                        "PIP_INDEX_URL": "https://packages.example/simple",
                        "PATH": f"{root / 'bin'}:{os.environ['PATH']}"},
                    capture_output=True, text=True,
                )
                log = calls.read_text()
                root_paths = {str(ROOT).replace("\\", "/")}
                if os.name == "nt":
                    drive, tail = os.path.splitdrive(str(ROOT))
                    root_paths.add("/" + drive[0].lower() + tail.replace(os.sep, "/"))
                self.assertTrue(
                    any(f"--index-url https://packages.example/simple --editable {path}/host/python" in log
                        for path in root_paths),
                    log,
                )
                self.assertIn("-m picopwm.app", log)
                if pip_available == "1":
                    self.assertIn(f"base -m pip --python {interpreter}", log)


if __name__ == "__main__":
    unittest.main()
