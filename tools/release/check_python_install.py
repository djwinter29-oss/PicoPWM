"""Verify an installed release's version, CLI startup and bundled dashboard over HTTP."""

from __future__ import annotations

import argparse
from importlib.metadata import distribution
import os
from pathlib import Path
import socket
import subprocess
import sysconfig
import tempfile
import time
from urllib.error import URLError
from urllib.request import build_opener, ProxyHandler, Request


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    package = distribution("picopwm")
    if package.version != args.version:
        raise RuntimeError(f"Expected picopwm {args.version}, found {package.version}")
    assets = [file for file in package.files or ()
              if str(file).startswith("picopwm/web/")]
    required = {
        "index.html", "favicon.svg", "css/dashboard.css", "css/web.css", "css/webhid.css",
        "js/app.mjs", "js/client.mjs", "js/device.mjs", "js/view.mjs",
    }
    missing = required - {str(file).removeprefix("picopwm/web/") for file in assets}
    if missing:
        raise RuntimeError(f"Installed dashboard is missing assets: {sorted(missing)}")
    executable = Path(sysconfig.get_path("scripts")) / (
        "picopwm.exe" if os.name == "nt" else "picopwm"
    )
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("PICOPWM_") and key != "PYTHONPATH"}
    env.update(PICOPWM_HOST="127.0.0.1", PICOPWM_PORT=str(port))
    opener = build_opener(ProxyHandler({}))
    url = f"http://localhost:{port}/"
    with tempfile.TemporaryDirectory() as cwd, tempfile.TemporaryFile(mode="w+b") as log:
        process = subprocess.Popen([str(executable)], cwd=cwd, env=env,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 15
            while True:
                if process.poll() is not None:
                    raise RuntimeError(f"picopwm exited with {process.returncode}")
                try:
                    with opener.open(url, timeout=1) as response:
                        if response.status != 200:
                            raise RuntimeError(f"Dashboard returned HTTP {response.status}")
                    break
                except URLError:
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Dashboard did not become ready within 15 seconds")
                    time.sleep(0.1)
            for asset in assets:
                relative = str(asset).removeprefix("picopwm/web/")
                request = Request(url + relative)
                with opener.open(request, timeout=5) as response:
                    if response.status != 200 or response.read() != package.locate_file(asset).read_bytes():
                        raise RuntimeError(f"Dashboard asset missing or changed: {relative}")
            print(f"Passed: picopwm {args.version}, installed CLI and {len(assets)} dashboard assets")
        except Exception:
            log.seek(0)
            print(log.read().decode("utf-8", errors="replace"), flush=True)
            raise
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
