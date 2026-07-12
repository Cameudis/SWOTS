#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate the public release ZIP layout and metadata."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import zipfile

from build_release import TITLE_ID, release_files, write_archive

EXPECTED = {
    f"atmosphere/contents/{TITLE_ID}/exefs.nsp",
    f"atmosphere/contents/{TITLE_ID}/toolbox.json",
    "switch/.overlays/SWOTS.ovl",
    "SWOTS/LICENSE.txt",
    "SWOTS/THIRD_PARTY_NOTICES.txt",
}


def verify(path: Path) -> None:
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        if names != EXPECTED:
            raise SystemExit(
                f"invalid release layout: expected {sorted(EXPECTED)}, got {sorted(names)}"
            )
        for name in EXPECTED:
            if archive.getinfo(name).file_size == 0:
                raise SystemExit(f"empty release file: {name}")
        toolbox = json.loads(
            archive.read(f"atmosphere/contents/{TITLE_ID}/toolbox.json")
        )
        expected_toolbox = {
            "name": "SWOTS Renderer",
            "tid": TITLE_ID,
            "requires_reboot": False,
        }
        if toolbox != expected_toolbox:
            raise SystemExit(f"invalid toolbox metadata: {toolbox!r}")
        forbidden = ("research/", "build/", ".git/", "libs/")
        if any(name.startswith(forbidden) for name in names):
            raise SystemExit("release contains repository-only files")

    checksum = path.with_suffix(path.suffix + ".sha256")
    if checksum.exists():
        fields = checksum.read_text(encoding="ascii").split()
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if fields != [actual, path.name]:
            raise SystemExit(f"invalid SHA-256 file: {checksum}")
    print(f"release layout verified: {path}")


def self_test() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        sysmodule = root / "renderer.nsp"
        overlay = root / "frontend.ovl"
        sysmodule.write_bytes(b"test-nsp")
        overlay.write_bytes(b"test-ovl")
        archive = root / "release.zip"
        write_archive(archive, release_files(sysmodule, overlay))
        verify(archive)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path, nargs="?")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    elif args.archive:
        verify(args.archive)
    else:
        parser.error("provide an archive or --self-test")


if __name__ == "__main__":
    main()
