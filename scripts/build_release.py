#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a deterministic, SD-card-ready SWOTS release archive."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import zipfile

TITLE_ID = "4200000000007E09"
FIXED_TIME = (2020, 1, 1, 0, 0, 0)
ROOT = Path(__file__).resolve().parent.parent


def release_files(sysmodule: Path, overlay: Path) -> dict[str, bytes]:
    toolbox = {
        "name": "SWOTS Renderer",
        "tid": TITLE_ID,
        "requires_reboot": False,
    }
    return {
        f"atmosphere/contents/{TITLE_ID}/exefs.nsp": sysmodule.read_bytes(),
        f"atmosphere/contents/{TITLE_ID}/toolbox.json": (
            json.dumps(toolbox, indent=4) + "\n"
        ).encode(),
        "switch/.overlays/SWOTS.ovl": overlay.read_bytes(),
        "SWOTS/LICENSE.txt": (ROOT / "LICENSE").read_bytes(),
        "SWOTS/THIRD_PARTY_NOTICES.txt": (
            ROOT / "THIRD_PARTY_NOTICES.md"
        ).read_bytes(),
    }


def write_archive(destination: Path, files: dict[str, bytes]) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as tmp:
        temporary = Path(tmp.name)
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name in sorted(files):
                info = zipfile.ZipInfo(name, FIXED_TIME)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                archive.writestr(info, files[name])
        shutil.move(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def stage_files(destination: Path, files: dict[str, bytes]) -> dict[str, bytes]:
    """Materialize the exact SD tree, removing anything left by an older build."""
    if destination.exists():
        shutil.rmtree(destination)
    for name, content in files.items():
        path = destination / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    return {
        path.relative_to(destination).as_posix(): path.read_bytes()
        for path in sorted(destination.rglob("*"))
        if path.is_file()
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--sysmodule", type=Path, required=True)
    parser.add_argument("--overlay", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=Path("dist"))
    args = parser.parse_args()

    if not args.version or any(c.isspace() for c in args.version):
        parser.error("version must be non-empty and contain no whitespace")
    for artifact in (args.sysmodule, args.overlay):
        if not artifact.is_file() or artifact.stat().st_size == 0:
            parser.error(f"missing or empty artifact: {artifact}")

    archive = args.output_dir / f"SWOTS-v{args.version}.zip"
    staged = stage_files(
        args.output_dir / "staging",
        release_files(args.sysmodule, args.overlay),
    )
    write_archive(archive, staged)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksum = archive.with_suffix(archive.suffix + ".sha256")
    checksum.write_text(f"{digest}  {archive.name}\n", encoding="ascii")
    print(f"release: {archive}")
    print(f"sha256: {checksum}")


if __name__ == "__main__":
    main()
