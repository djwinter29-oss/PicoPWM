"""Hardware-free launcher checks: python3 tools/test/test_run_dotnet.py."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RunDotnetTests(unittest.TestCase):
    def test_runtime_checks_and_port(self):
        for policy in ("", "Major"):
            with self.subTest(policy=policy), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                executable = root / "dotnet"
                executable.write_text(
                    '#!/bin/sh\n'
                    'case "$1" in\n'
                    '  --list-sdks) echo "10.0.113 [/sdk]" ;;\n'
                    '  --list-runtimes) echo "Microsoft.AspNetCore.App 10.0.13 [/runtime]" ;;\n'
                    '  run) printf "%s\\n%s\\n" "$ASPNETCORE_URLS" "$*" > "$CALLS" ;;\n'
                    'esac\n'
                )
                executable.chmod(0o755)
                calls = root / "calls"
                env = {**os.environ, "DOTNET": str(executable), "DOTNET_ROLL_FORWARD": policy,
                       "PICOPWM_PORT": "5002", "PICOPWM_HOST": "127.0.0.1", "CALLS": str(calls)}
                env.pop("ASPNETCORE_URLS", None)
                result = subprocess.run(
                    ["sh", str(ROOT / "tools/host/run-dotnet.sh")], env=env, capture_output=True, text=True)
                if policy:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIn("http://127.0.0.1:5002", calls.read_text())
                    self.assertIn("--no-launch-profile --", calls.read_text())
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("runtime is missing", result.stderr)
                    self.assertFalse(calls.exists())


if __name__ == "__main__":
    unittest.main()
