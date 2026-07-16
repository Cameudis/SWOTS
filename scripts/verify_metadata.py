#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep public identity, Title ID, and version metadata synchronized."""

from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXPECTED_NAME = "SWOTS"


def require_match(path: str, pattern: str, label: str) -> str:
    text = (ROOT / path).read_text(encoding="utf-8")
    match = re.search(pattern, text)
    if not match:
        raise SystemExit(f"metadata verification failed: {label} missing in {path}")
    return match.group(1)


def main() -> None:
    version = (ROOT / "VERSION").read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?", version):
        raise SystemExit(f"metadata verification failed: invalid VERSION {version!r}")

    npdm = json.loads((ROOT / "swots.json").read_text(encoding="utf-8"))
    if npdm.get("name") != EXPECTED_NAME:
        raise SystemExit("metadata verification failed: NPDM project name differs")
    if npdm.get("service_host") != ["swots:u"]:
        raise SystemExit(
            "metadata verification failed: renderer must host only swots:u"
        )
    if "*" not in npdm.get("service_access", []):
        raise SystemExit(
            "metadata verification failed: expected legacy wildcard service access"
        )
    title_id = npdm.get("title_id", "").removeprefix("0x").upper()
    if not re.fullmatch(r"[0-9A-F]{16}", title_id):
        raise SystemExit("metadata verification failed: invalid NPDM title ID")

    values = {
        "C++ title ID": require_match(
            "include/config.hpp", r"SWOTS_TITLE_ID\s*=\s*0x([0-9A-Fa-f]+)ULL", "title ID"
        ).upper(),
        "deploy title ID": require_match(
            "scripts/deploy.sh", r'TID="([0-9A-Fa-f]+)"', "title ID"
        ).upper(),
        "release title ID": require_match(
            "scripts/build_release.py", r'TITLE_ID\s*=\s*"([0-9A-Fa-f]+)"', "title ID"
        ).upper(),
    }
    mismatches = {label: value for label, value in values.items() if value != title_id}
    if mismatches:
        raise SystemExit(
            f"metadata verification failed: NPDM title ID {title_id}, mismatches {mismatches}"
        )

    print(f"metadata verified: {EXPECTED_NAME} v{version}, title ID {title_id}")


if __name__ == "__main__":
    main()
