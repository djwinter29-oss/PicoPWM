# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""Check staged Direct versioning, cache graph, source isolation, and local HTTP serving."""

import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from urllib.error import URLError
from urllib.request import build_opener, ProxyHandler


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("direct_site", ROOT / "tools/host/direct_site.py")
site = importlib.util.module_from_spec(spec)
spec.loader.exec_module(site)


class DirectSiteTests(unittest.TestCase):
    def test_staged_versions_cache_keys_and_source_isolation(self):
        source = site.SOURCE
        original = {path: path.read_bytes() for path in source.rglob("*") if path.is_file()}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, version in (("release", "1.2.3"), ("test", "0.0.0-test.123.1")):
                output = root / name
                site.build(source, output, version, "a" * 40, False)
                site.verify(output, version)
                metadata = json.loads((output / "build.json").read_text())
                page = (output / "index.html").read_text()
                self.assertIn(f">{version}</span>", page)
                self.assertIn('title="' + "a" * 40 + '"', page)
                self.assertNotIn("0.0.0-debug", page)
                device_keys = []
                for module in ("app.mjs", "view.mjs"):
                    text = (output / "js" / module).read_text()
                    device_keys.append(text.split("./device.mjs?v=", 1)[1].split('"', 1)[0])
                self.assertEqual([metadata["cache_key"]] * 2, device_keys)
                site.build(source, root / (name + "-again"), version, "a" * 40, False)
                self.assertEqual((output / "build.json").read_bytes(),
                                 (root / (name + "-again") / "build.json").read_bytes())
            self.assertNotEqual(json.loads((root / "release/build.json").read_text())["cache_key"],
                                json.loads((root / "test/build.json").read_text())["cache_key"])
        self.assertEqual(original, {path: path.read_bytes() for path in original})

    def test_asset_edits_change_the_whole_graph_cache_key(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            shutil.copytree(site.SOURCE, source)
            site.build(source, root / "before", "1.2.3", "a" * 40, False)
            path = source / "js/device.mjs"
            path.write_text(path.read_text() + "\n", encoding="utf-8")
            site.build(source, root / "after", "1.2.3", "a" * 40, False)
            self.assertNotEqual((root / "before/build.json").read_bytes(),
                                (root / "after/build.json").read_bytes())
            self.assertNotEqual((root / "before/index.html").read_bytes(),
                                (root / "after/index.html").read_bytes())

    def test_final_artifact_verification_rejects_tampering(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "site"
            site.build(site.SOURCE, output, "1.2.3", "a" * 40, False)
            with self.assertRaisesRegex(RuntimeError, "version differs"):
                site.verify(output, "1.2.4")
            path = output / "js/view.mjs"
            original = path.read_text()
            path.write_text(original.replace("./device.mjs?v=", "./device.mjs?v=wrong"))
            with self.assertRaisesRegex(RuntimeError, "Inconsistent cache key"):
                site.verify(output, "1.2.3")
            path.write_text(original)
            (output / "js/device.mjs").unlink()
            with self.assertRaisesRegex(RuntimeError, "Missing or invalid asset"):
                site.verify(output, "1.2.3")
            with self.assertRaises(ValueError):
                site.build(site.SOURCE, Path(temporary) / "bad", "<invalid>", "a", False)
            with self.assertRaises(FileExistsError):
                site.build(site.SOURCE, output, "1.2.3", "a", False)
            with self.assertRaises(ValueError):
                site.build(site.SOURCE, site.SOURCE / "output", "1.2.3", "a", False)

    def test_git_release_development_dirty_and_archive_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
            env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)

            def git(*args):
                subprocess.run(["git", "-c", "user.name=Version Test", "-c", "user.email=test@example.invalid",
                                "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false",
                                "-c", f"core.hooksPath={os.devnull}", "-C", str(root), *args],
                               check=True, env=env, capture_output=True)

            git("init", "--quiet")
            git("commit", "--allow-empty", "--quiet", "-m", "fixture")
            self.assertTrue(site.identity(root, None)[0].startswith("0.0.0-dev.1+g"))
            git("tag", "v1.2.3")
            version, commit, dirty = site.identity(root, None)
            self.assertEqual(f"1.2.3+g{commit[:12]}", version)
            self.assertFalse(dirty)
            git("commit", "--allow-empty", "--quiet", "-m", "next")
            git("tag", "v1.2.4-rc1")
            version, commit, dirty = site.identity(root, None)
            self.assertEqual(f"1.2.4-dev.1+g{commit[:12]}", version)
            (root / "draft.txt").write_text("dirty", encoding="utf-8")
            self.assertTrue(site.identity(root, None)[0].endswith(".dirty"))
            self.assertEqual("0.0.0-test.1.1", site.identity(root, "0.0.0-test.1.1")[0])
        with tempfile.TemporaryDirectory() as temporary:
            self.assertEqual(("0.0.0-debug", "unknown", False), site.identity(Path(temporary), None))

    def test_loopback_server_binds_without_reverse_dns(self):
        with patch("socket.getfqdn", side_effect=AssertionError("Loopback startup must not resolve DNS")):
            with site.DevelopmentServer(("127.0.0.1", 0), site.DevelopmentHandler) as server:
                self.assertEqual("127.0.0.1", server.server_name)
                self.assertGreater(server.server_port, 0)
                self.assertEqual(server.server_address[1], server.server_port)

    def test_local_server_serves_staged_identity_and_stops_cleanly(self):
        opener = build_opener(ProxyHandler({}))
        with tempfile.TemporaryDirectory() as cwd, open(Path(cwd) / "server.log", "w+b") as log:
            # Dump the child's stack before the readiness deadline if startup stalls in CI.
            diagnostic = ("import faulthandler, runpy, sys; "
                          "faulthandler.dump_traceback_later(20, repeat=False); "
                          "script = sys.argv.pop(1); runpy.run_path(script, run_name='__main__')")
            process = subprocess.Popen([sys.executable, "-u", "-c", diagnostic,
                                        str(ROOT / "tools/host/direct_site.py"),
                                        "serve", "--port", "0"], cwd=cwd,
                                       stdout=log, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0)
            try:
                def output():
                    return (Path(cwd) / "server.log").read_text(encoding="utf-8", errors="replace")

                deadline = time.monotonic() + 30
                while True:
                    text = output()
                    self.assertIsNone(process.poll(), f"Direct server exited early:\n{text}")
                    ready = re.search(r"Direct Dashboard .+ at (http://127\.0\.0\.1:[1-9]\d*/)", text)
                    if ready:
                        url = ready[1]
                        break
                    if time.monotonic() >= deadline:
                        self.fail(f"Direct server did not announce readiness within 30 seconds:\n{text}")
                    time.sleep(0.1)
                while True:
                    try:
                        with opener.open(url + "build.json", timeout=2) as response:
                            metadata = json.load(response)
                        break
                    except (URLError, TimeoutError) as error:
                        if time.monotonic() >= deadline:
                            self.fail(f"Direct server announced readiness but HTTP failed: {error}\n{output()}")
                        time.sleep(0.1)
                with opener.open(url, timeout=5) as response:
                    self.assertEqual("no-store", response.headers["Cache-Control"])
                    page = response.read().decode()
                self.assertIn(metadata["version"], page)
                self.assertIn(metadata["cache_key"], page)
                self.assertNotEqual("unknown", metadata["commit"])
                with opener.open(f"{url}js/app.mjs?v={metadata['cache_key']}",
                                 timeout=5) as response:
                    self.assertIn(metadata["cache_key"], response.read().decode())
                process.send_signal(signal.CTRL_BREAK_EVENT if sys.platform == "win32" else signal.SIGTERM)
                self.assertEqual(0, process.wait(timeout=10))
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=10)
                log.seek(0)
                if process.returncode:
                    print(log.read().decode(errors="replace"), file=sys.stderr)


if __name__ == "__main__":
    unittest.main()
