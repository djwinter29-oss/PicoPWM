# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong

"""Stage, verify, or locally serve Direct Dashboard with build identity and cache keys."""

from __future__ import annotations

import argparse
from functools import partial
import hashlib
import html
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
from urllib.parse import parse_qsl, urlencode, urlsplit, urlunsplit


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "host/direct/src"
VERSION = re.compile(r"(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)"
                     r"(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?")
MARKER = 'aria-label="Host application version">0.0.0-debug</span>'
LINKS = {
    ".html": re.compile(r"""(?:href|src)=["'](?P<url>[^"']+)["']"""),
    ".mjs": re.compile(r"""(?:\bfrom\s*|\bimport\s*(?:\(\s*)?)["'](?P<url>[^"']+)["']"""),
    ".css": re.compile(r"""url\(\s*["']?(?P<url>[^"')\s]+)["']?\s*\)"""),
}


def git(root: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(root), *args], check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.strip()


def identity(root: Path, version: str | None) -> tuple[str, str, bool]:
    """Derive development identity; explicit deployment versions still retain their commit."""
    try:
        commit = git(root, "rev-parse", "HEAD")
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        print(f"Git identity unavailable; using source-archive identity: {error}", file=sys.stderr)
        return version or "0.0.0-debug", "unknown", False
    dirty = bool(git(root, "status", "--porcelain", "--untracked-files=normal"))
    if version is not None:
        return version, commit, dirty
    tags = git(root, "tag", "--list", "--merged", "HEAD")
    if any(re.fullmatch(r"v\d+\.\d+\.\d+", tag) for tag in tags.splitlines()):
        description = git(root, "describe", "--tags", "--long", "--match", "v[0-9]*.[0-9]*.[0-9]*",
                          "--exclude", "v*-*", "--exclude", "v*+*")
        match = re.fullmatch(r"v(\d+)\.(\d+)\.(\d+)-(\d+)-g([0-9a-f]+)", description)
        if match is None:
            raise RuntimeError(f"Unsupported version tag in Git description: {description}")
        major, minor, patch, height = map(int, match.groups()[:4])
        version = (f"{major}.{minor}.{patch}" if height == 0
                   else f"{major}.{minor}.{patch + 1}-dev.{height}")
    else:
        version = f"0.0.0-dev.{git(root, 'rev-list', '--count', 'HEAD')}"
    return version + f"+g{commit[:12]}" + (".dirty" if dirty else ""), commit, dirty


def local_asset(base: Path, url: str, root: Path) -> Path | None:
    """Resolve relative asset references; navigation, anchors, and remote links are not assets."""
    parts = urlsplit(url)
    if parts.scheme or parts.netloc or not parts.path or parts.path.endswith("/"):
        return None
    target = (base.parent / parts.path).resolve()
    if parts.path.startswith("/") or not target.is_relative_to(root.resolve()) or not target.is_file():
        raise RuntimeError(f"Missing or invalid asset reference in {base.name}: {url}")
    return target


def verify(output: Path, expected: str) -> None:
    """Check the final stamped artifact and its complete local reference graph."""
    metadata = json.loads((output / "build.json").read_text(encoding="utf-8"))
    page = (output / "index.html").read_text(encoding="utf-8")
    if metadata["version"] != expected or MARKER in page:
        raise RuntimeError("Staged dashboard version differs from the requested version")
    label = f'aria-label="Host application version" title="{html.escape(metadata["commit"], quote=True)}">'
    if label + html.escape(expected) + "</span>" not in page:
        raise RuntimeError("Staged dashboard version/commit label is missing")
    for path in output.rglob("*"):
        if path.suffix not in LINKS:
            continue
        for match in LINKS[path.suffix].finditer(path.read_text(encoding="utf-8")):
            url = match["url"]
            if local_asset(path, url, output) is not None:
                values = [value for key, value in parse_qsl(urlsplit(url).query) if key == "v"]
                if values != [metadata["cache_key"]]:
                    raise RuntimeError(f"Inconsistent cache key in {path.name}: {url}")


def build(source: Path, output: Path, version: str, commit: str, dirty: bool) -> None:
    """Copy canonical assets and stamp only the staged copy."""
    if VERSION.fullmatch(version) is None:
        raise ValueError(f"Invalid dashboard version: {version}")
    if output.resolve().is_relative_to(source.resolve()) or source.resolve().is_relative_to(output.resolve()):
        raise ValueError("Staging output must not overlap canonical source")
    if output.exists():
        raise FileExistsError(f"Staging output already exists: {output}")
    digest = hashlib.sha256(json.dumps([version, commit, dirty]).encode())
    for path in sorted(source.rglob("*")):
        if path.is_file():
            digest.update(path.relative_to(source).as_posix().encode() + b"\0" + path.read_bytes() + b"\0")
    key = digest.hexdigest()[:16]
    shutil.copytree(source, output)
    page = output / "index.html"
    text = page.read_text(encoding="utf-8")
    if text.count(MARKER) != 1:
        raise RuntimeError("Expected exactly one source version marker")
    text = text.replace(MARKER, f'aria-label="Host application version" title="{html.escape(commit, quote=True)}">'
                        f'{html.escape(version)}</span>')
    page.write_text(text, encoding="utf-8")
    for path in output.rglob("*"):
        if path.suffix not in LINKS:
            continue

        def stamp(match: re.Match) -> str:
            url = match["url"]
            if local_asset(path, url, output) is None:
                return match[0]
            parts = urlsplit(url)
            query = [(name, value) for name, value in parse_qsl(parts.query) if name != "v"]
            query.append(("v", key))
            replacement = urlunsplit(parts._replace(query=urlencode(query)))
            start, end = match.span("url")
            return match[0][:start - match.start()] + replacement + match[0][end - match.start():]

        path.write_text(LINKS[path.suffix].sub(stamp, path.read_text(encoding="utf-8")), encoding="utf-8")
    (output / "build.json").write_text(json.dumps({
        "version": version, "commit": commit, "dirty": dirty, "cache_key": key,
    }, indent=2) + "\n", encoding="utf-8")
    verify(output, version)


class DevelopmentHandler(SimpleHTTPRequestHandler):
    """Revalidate local HTML as well as assets after restarting the staged server."""

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    staging = commands.add_parser("build")
    staging.add_argument("--output", type=Path, required=True)
    staging.add_argument("--version", required=True)
    checking = commands.add_parser("verify")
    checking.add_argument("--output", type=Path, required=True)
    checking.add_argument("--version", required=True)
    serving = commands.add_parser("serve")
    serving.add_argument("--port", type=int, default=8000, help="Listen port; 0 selects an available port")
    args = parser.parse_args()
    if args.command == "verify":
        verify(args.output.resolve(), args.version)
        return
    version, commit, dirty = identity(ROOT, args.version if args.command == "build" else None)
    if args.command == "build":
        build(SOURCE, args.output.resolve(), version, commit, dirty)
        print(f"Verified Direct Dashboard {version} ({commit}) in {args.output}")
        return
    if not 0 <= args.port <= 65535:
        parser.error("port must be between 0 and 65535")
    with tempfile.TemporaryDirectory(prefix="picopwm-direct-") as temporary:
        output = Path(temporary) / "site"
        build(SOURCE, output, version, commit, dirty)
        handler = partial(DevelopmentHandler, directory=str(output))
        with ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
            def terminate(signum, frame):
                raise KeyboardInterrupt

            signal.signal(signal.SIGTERM, terminate)
            if sys.platform == "win32":
                signal.signal(signal.SIGBREAK, terminate)
            print(f"Direct Dashboard {version} at http://127.0.0.1:{server.server_port}/", flush=True)
            try:
                server.serve_forever()
            except KeyboardInterrupt:
                pass


if __name__ == "__main__":
    main()
