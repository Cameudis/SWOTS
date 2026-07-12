#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject NPDM capability changes that break the tested old Atmosphere/HOS."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


# Byte-for-byte capability baseline taken from the working old-system build.
# The final word must remain ff ff 04 00: npdmtool emits a different word if
# swots.json's force_debug_prod/force_debug combination is changed.
EXPECTED_KAC = bytes.fromhex(
    "f7430003 efffff1f efffff3f ef07e647 efffff7f "
    "effff79f ef1f00a0 ff3f1800 ff7fff03 ffff0400"
)
EXPECTED_DEBUG_CAPABILITY = bytes.fromhex("ffff0400")


class VerificationError(Exception):
    pass


def u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise VerificationError(f"u32 at 0x{offset:x} is outside the file")
    return struct.unpack_from("<I", data, offset)[0]


def checked_slice(data: bytes, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise VerificationError(
            f"{label} range 0x{offset:x}..0x{offset + size:x} is outside the file"
        )
    return data[offset : offset + size]


def extract_pfs0_file(data: bytes, filename: str) -> bytes:
    if data[:4] != b"PFS0":
        raise VerificationError("NSP does not start with PFS0")

    file_count = u32(data, 4)
    string_table_size = u32(data, 8)
    entries_offset = 0x10
    strings_offset = entries_offset + file_count * 0x18
    payload_offset = strings_offset + string_table_size
    strings = checked_slice(data, strings_offset, string_table_size, "PFS0 strings")

    for index in range(file_count):
        entry = entries_offset + index * 0x18
        file_offset, file_size, name_offset = struct.unpack_from("<QQI", data, entry)
        if name_offset >= len(strings):
            raise VerificationError("PFS0 filename offset is outside the string table")
        name_end = strings.find(b"\0", name_offset)
        if name_end < 0:
            raise VerificationError("PFS0 filename is not NUL-terminated")
        name = strings[name_offset:name_end].decode("utf-8")
        if name == filename:
            return checked_slice(
                data, payload_offset + file_offset, file_size, f"PFS0 {filename}"
            )

    raise VerificationError(f"NSP does not contain {filename}")


def extract_kacs(npdm: bytes) -> tuple[bytes, bytes]:
    if npdm[:4] != b"META":
        raise VerificationError("NPDM does not start with META")

    acid_offset, acid_size = u32(npdm, 0x78), u32(npdm, 0x7C)
    aci0_offset, aci0_size = u32(npdm, 0x70), u32(npdm, 0x74)
    acid = checked_slice(npdm, acid_offset, acid_size, "ACID")
    aci0 = checked_slice(npdm, aci0_offset, aci0_size, "ACI0")

    # ACID starts with a 0x200-byte signature/key area; its section offsets are
    # nevertheless relative to the beginning of the complete ACID blob.
    if acid[0x200:0x204] != b"ACID":
        raise VerificationError("ACID magic is missing")
    acid_kac_offset = u32(acid, 0x230)
    acid_kac_size = u32(acid, 0x234)
    acid_kac = checked_slice(acid, acid_kac_offset, acid_kac_size, "ACID KAC")

    if aci0[:4] != b"ACI0":
        raise VerificationError("ACI0 magic is missing")
    aci0_kac_offset = u32(aci0, 0x30)
    aci0_kac_size = u32(aci0, 0x34)
    aci0_kac = checked_slice(aci0, aci0_kac_offset, aci0_kac_size, "ACI0 KAC")
    return acid_kac, aci0_kac


def verify_npdm(npdm: bytes, label: str) -> None:
    acid_kac, aci0_kac = extract_kacs(npdm)
    for section, actual in (("ACID", acid_kac), ("ACI0", aci0_kac)):
        if actual != EXPECTED_KAC:
            debug_hint = ""
            if actual[-4:] != EXPECTED_DEBUG_CAPABILITY:
                debug_hint = (
                    f"; debug capability is {actual[-4:].hex()}, expected "
                    f"{EXPECTED_DEBUG_CAPABILITY.hex()}"
                )
            raise VerificationError(
                f"{label} {section} KAC changed: {actual.hex()} "
                f"!= {EXPECTED_KAC.hex()}{debug_hint}"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("npdm", type=Path, help="generated standalone NPDM")
    parser.add_argument("nsp", type=Path, help="generated ExeFS PFS0 package")
    args = parser.parse_args()

    try:
        standalone = args.npdm.read_bytes()
        packaged = extract_pfs0_file(args.nsp.read_bytes(), "main.npdm")
        verify_npdm(standalone, str(args.npdm))
        verify_npdm(packaged, f"{args.nsp}:main.npdm")
        if packaged != standalone:
            raise VerificationError("packaged main.npdm differs from standalone NPDM")
    except (OSError, UnicodeError, struct.error, VerificationError) as error:
        print(f"NPDM verification failed: {error}", file=sys.stderr)
        return 1

    print(
        "verified: NPDM ACID/ACI0 KAC matches old-system baseline "
        f"(debug={EXPECTED_DEBUG_CAPABILITY.hex()})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
