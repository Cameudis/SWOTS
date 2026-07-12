#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Stage/install both the dynamically launchable sysmodule and Tesla frontend.
set -euo pipefail
cd "$(dirname "$0")/.."

TID="4200000000007E09"

if [ ! -f swots.nsp ] || [ ! -f tesla-overlay/swots.ovl ]; then
    echo "error: demo not built — run 'make demo' first" >&2
    exit 1
fi

DEST=""
if [ "${1:-}" = "--dbi" ]; then
    GVFS="/run/user/$(id -u)/gvfs"
    for d in "$GVFS"/mtp:host=-_DBI_*; do
        if [ -d "$d" ]; then DEST="$d/1: SD Card"; break; fi
    done
    if [ -z "$DEST" ] || [ ! -d "$DEST" ]; then
        echo "error: DBI gvfs-mtp share not found under $GVFS" >&2
        exit 1
    fi
elif [ $# -ge 1 ]; then
    DEST="$1"
else
    echo "usage: $0 <sd-mount-or-staging-dir> | --dbi" >&2
    exit 1
fi

BASE="$DEST/atmosphere/contents/$TID"
OVERLAYS="$DEST/switch/.overlays"
mkdir -p "$BASE" "$OVERLAYS"

# Atmosphere registers the program from this ExeFS package. There is no
# boot2.flag: the SWOTS Tesla overlay launches it dynamically via pmshell.
cp swots.nsp "$BASE/exefs.nsp"
cp tesla-overlay/swots.ovl "$OVERLAYS/SWOTS.ovl"

# Remove the pre-release filename so Tesla does not show duplicate entries.
rm -f "$OVERLAYS/KineStop.ovl"
rm -f "$OVERLAYS/MotionCues.ovl"

# Older prototypes used boot2 auto-start. Remove only this project's stale
# flag so the dynamic Tesla launch path is what gets exercised after reboot.
rm -f "$BASE/flags/boot2.flag"

cat > "$BASE/toolbox.json" <<EOF
{
    "name": "SWOTS Renderer",
    "tid": "$TID",
    "requires_reboot": false
}
EOF

echo "deployed SWOTS:"
echo "  $BASE/exefs.nsp"
echo "  $BASE/toolbox.json"
echo "  $OVERLAYS/SWOTS.ovl"
echo
echo "Open Tesla -> SWOTS -> Motion cues: On"
