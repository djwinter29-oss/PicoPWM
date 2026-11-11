# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""Smoke-test a published .NET executable, version, dashboard assets, and graceful shutdown."""

from __future__ import annotations

import argparse
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import unquote, urljoin, urlsplit
from urllib.request import build_opener, ProxyHandler


class AssetReferences(HTMLParser):
    """Local page references, including cache-busting query strings."""

    def __init__(self) -> None:
        super().__init__()
        self.urls: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self.urls.extend(value for name, value in attrs if name in ("src", "href") and value)


def references(path: Path) -> list[str]:
    """Find references in the published HTML, static ES modules, and CSS."""
    text = path.read_text(encoding="utf-8")
    if path.suffix == ".html":
        parser = AssetReferences()
        parser.feed(text)
        return parser.urls
    if path.suffix in (".mjs", ".js"):
        return re.findall(r"""(?:\bfrom\s*|\bimport\s*(?:\(\s*)?)["']([^"']+)["']""", text)
    if path.suffix == ".css":
        return [match[1] for match in re.findall(r"""url\(\s*(["']?)([^"')\s]+)\1\s*\)""", text)]
    return []


def check_assets(web_root: Path, fetch) -> int:
    """Verify every bundled asset and each local HTML/module/CSS reference over HTTP."""
    index = web_root / "index.html"
    if not index.is_file():
        raise RuntimeError("Published dashboard is missing index.html")
    if fetch("/") != index.read_bytes():
        raise RuntimeError("Dashboard root did not serve the published index.html")
    # SDK .br/.gz sidecars are encodings, not public URLs in UseStaticFiles.
    urls = {"/" + path.relative_to(web_root).as_posix()
            for path in web_root.rglob("*") if path.is_file() and path.suffix not in (".br", ".gz")}
    for path in web_root.rglob("*"):
        if not path.is_file() or path.suffix not in (".html", ".js", ".mjs", ".css"):
            continue
        base = "/" + path.relative_to(web_root).as_posix()
        for reference in references(path):
            parts = urlsplit(reference)
            if parts.scheme or parts.netloc or reference.startswith("#"):
                continue
            urls.add(urljoin(base, reference))
    for url in sorted(urls):
        relative = unquote(urlsplit(url).path).lstrip("/")
        asset = (web_root / (relative or "index.html")).resolve()
        if not asset.is_relative_to(web_root.resolve()) or not asset.is_file():
            raise RuntimeError(f"Dashboard references a missing or invalid asset: {url}")
        if fetch(url) != asset.read_bytes():
            raise RuntimeError(f"Dashboard asset missing or changed over HTTP: {url}")
    return len(urls)


def stop(process: subprocess.Popen) -> None:
    """Require a graceful zero exit; forced cleanup is never a passing shutdown."""
    if process.poll() is not None:
        raise RuntimeError(f"Published host exited before shutdown: {process.returncode}")
    process.send_signal(signal.CTRL_BREAK_EVENT if sys.platform == "win32" else signal.SIGTERM)
    try:
        code = process.wait(timeout=15)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("Published host did not shut down gracefully within 15 seconds") from error
    if code != 0:
        raise RuntimeError(f"Published host shutdown returned {code}, expected 0")


def check(directory: Path, version: str) -> None:
    executable = directory / ("PicoPwm.exe" if sys.platform == "win32" else "PicoPwm")
    if not executable.is_file():
        raise RuntimeError(f"Published executable missing: {executable}")
    env = {key: value for key, value in os.environ.items()
           if not key.upper().startswith(("PICOPWM_", "ASPNETCORE_", "DOTNET_"))}
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    env.update(ASPNETCORE_URLS=f"http://127.0.0.1:{port}", ASPNETCORE_ENVIRONMENT="Production")
    opener = build_opener(ProxyHandler({}))

    def fetch(path: str) -> bytes:
        try:
            with opener.open(f"http://127.0.0.1:{port}{path}", timeout=2) as response:
                if response.status != 200:
                    raise RuntimeError(f"HTTP {response.status} for {path}")
                return response.read()
        except HTTPError as error:
            raise RuntimeError(f"HTTP {error.code} for {path}") from error

    # Windows control events require a shared console, even on a headless Actions runner.
    allocated_console = False
    if sys.platform == "win32":
        import ctypes

        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        if not kernel.GetConsoleCP():
            if not kernel.AllocConsole():
                raise ctypes.WinError(ctypes.get_last_error())
            allocated_console = True
    try:
        with tempfile.TemporaryDirectory() as cwd, tempfile.TemporaryFile(mode="w+b") as log:
            process = subprocess.Popen(
                [str(executable)], cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0,
            )
            try:
                deadline = time.monotonic() + 30
                while True:
                    if process.poll() is not None:
                        raise RuntimeError(f"Published host exited during startup: {process.returncode}")
                    try:
                        metadata = json.loads(fetch("/api/host"))
                        break
                    except (URLError, TimeoutError):
                        if time.monotonic() >= deadline:
                            raise RuntimeError("Published host did not become ready within 30 seconds")
                        time.sleep(0.1)
                actual = metadata.get("version")
                if (metadata.get("dashboard_type") != ".NET" or not isinstance(actual, str)
                        or not (actual == version or ("+" not in version and actual.startswith(version + "+")))):
                    raise RuntimeError(f"Expected .NET {version}, received {metadata}")
                count = check_assets(directory / "wwwroot", fetch)
                stop(process)
                print(f"Passed: .NET {actual}, {count} asset URLs, graceful shutdown")
            except Exception:
                log.seek(0, os.SEEK_END)
                log.seek(max(0, log.tell() - 16000))
                print(log.read().decode("utf-8", errors="replace"), file=sys.stderr, flush=True)
                raise
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=10)
    finally:
        if allocated_console:
            kernel.FreeConsole()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    check(args.directory.resolve(), args.version)


if __name__ == "__main__":
    main()
