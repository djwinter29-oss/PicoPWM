#!/usr/bin/env python3
"""Report the USB identity configured by the PicoPWM firmware."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _usb_identity(repo_root: Path) -> tuple[int, int]:
    descriptor_path = repo_root / "firmware" / "src" / "usb" / "usb_descriptors.c"
    source = descriptor_path.read_text(encoding="utf-8")
    values = {}
    for name in ("USB_VID", "USB_PID"):
        match = re.search(rf"^#define\s+{name}\s+0x([0-9A-Fa-f]+)\s*$", source, re.MULTILINE)
        if match is None:
            raise ValueError(f"could not find {name} in {descriptor_path}")
        values[name] = int(match.group(1), 16)
    return values["USB_VID"], values["USB_PID"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=_repo_root(),
        help="Repository root containing firmware/src/usb/usb_descriptors.c",
    )
    args = parser.parse_args()

    try:
        vid, pid = _usb_identity(args.repo_root)
    except (OSError, ValueError) as error:
        parser.error(str(error))

    print(f"USB identity {vid:#06x}:{pid:#06x}")
    if (vid, pid) == (0xCAFE, 0x4010):
        print("Using the unallocated lab USB identity reserved for development builds.")
    else:
        print("Allocated USB identity configured.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
