# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""Hardware-free checks for the published .NET smoke-test runner."""

import importlib.util
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("check_dotnet_publish", ROOT / "tools/release/check_dotnet_publish.py")
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


class DotnetPublishTests(unittest.TestCase):
    def test_assets_and_transitive_cache_busted_references(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            files = {
                "index.html": '<script type="module" src="js/app.mjs?v=1"></script><link href="css/app.css">',
                "js/app.mjs": 'import "./other.mjs?v=2"; export { value } from "./other.mjs";',
                "js/other.mjs": 'export const value = 1;',
                "css/app.css": 'body { background: url("../image.svg?v=3"); }',
                "image.svg": '<svg/>',
            }
            for relative, content in files.items():
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            (root / "index.html.br").write_bytes(b"SDK compressed sidecar, not a public URL")
            (root / "index.html.gz").write_bytes(b"SDK compressed sidecar, not a public URL")
            requested = []

            def fetch(url):
                requested.append(url)
                relative = url.split("?", 1)[0].lstrip("/") or "index.html"
                return (root / relative).read_bytes()

            self.assertEqual(8, smoke.check_assets(root, fetch))
            self.assertIn("/js/other.mjs?v=2", requested)
            self.assertIn("/image.svg?v=3", requested)
            (root / "js/other.mjs").unlink()
            with self.assertRaisesRegex(RuntimeError, "missing or invalid asset"):
                smoke.check_assets(root, fetch)

    def test_missing_index_and_changed_http_content_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(RuntimeError, "missing index"):
                smoke.check_assets(root, Mock())
            (root / "index.html").write_text("dashboard", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "root did not serve"):
                smoke.check_assets(root, lambda _: b"wrong")
            (root / "app.mjs").write_text("export {};", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "asset missing or changed"):
                smoke.check_assets(root, lambda url: b"dashboard" if url == "/" else b"wrong")

    def test_shutdown_requires_zero_exit_after_graceful_signal(self):
        process = Mock()
        process.poll.return_value = None
        process.wait.return_value = 0
        smoke.stop(process)
        expected = signal.CTRL_BREAK_EVENT if sys.platform == "win32" else signal.SIGTERM
        process.send_signal.assert_called_once_with(expected)
        process.wait.assert_called_once_with(timeout=15)
        process.wait.return_value = 1
        with self.assertRaisesRegex(RuntimeError, "returned 1"):
            smoke.stop(process)
        process.wait.side_effect = subprocess.TimeoutExpired("PicoPwm", 15)
        with self.assertRaisesRegex(RuntimeError, "did not shut down gracefully"):
            smoke.stop(process)
        process.poll.return_value = 0
        with self.assertRaisesRegex(RuntimeError, "exited before shutdown"):
            smoke.stop(process)

    def test_version_failure_and_startup_exit_are_not_success_and_cleanup_runs(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / ("PicoPwm.exe" if sys.platform == "win32" else "PicoPwm")).touch()
            process = Mock()
            process.poll.side_effect = [None, None]
            response = Mock(status=200)
            response.read.return_value = b'{"dashboard_type":".NET","version":"9.9.9"}'
            opener = Mock()
            opener.open.return_value.__enter__ = Mock(return_value=response)
            opener.open.return_value.__exit__ = Mock(return_value=False)
            with patch.object(smoke, "build_opener", return_value=opener), \
                    patch.object(smoke.subprocess, "Popen", return_value=process):
                with self.assertRaisesRegex(RuntimeError, "Expected .NET"):
                    smoke.check(directory, "1.2.3")
            process.kill.assert_called_once()
            process.poll.side_effect = [2, 2]
            process.returncode = 2
            process.kill.reset_mock()
            with patch.object(smoke.subprocess, "Popen", return_value=process):
                with self.assertRaisesRegex(RuntimeError, "exited during startup"):
                    smoke.check(directory, "1.2.3")
            process.kill.assert_not_called()


if __name__ == "__main__":
    unittest.main()
