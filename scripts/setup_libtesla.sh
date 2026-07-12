#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

cd "$(dirname "$0")/.."

readonly repo="https://github.com/WerWolv/libtesla.git"
readonly revision="f766e9b607a05e9756843cbd62b3bfb98be1646c"
readonly destination="libs/libtesla"
readonly patch_file="$PWD/patches/libtesla-post-foreground-release.patch"

if [[ -e "$destination" && ! -d "$destination/.git" ]]; then
    echo "error: $destination exists but is not a Git checkout" >&2
    exit 1
fi

if [[ ! -d "$destination/.git" ]]; then
    mkdir -p libs
    git init -q "$destination"
    git -C "$destination" remote add origin "$repo"
fi

if [[ -n "$(git -C "$destination" status --porcelain --untracked-files=no)" ]]; then
    if git -C "$destination" apply --reverse --check "$patch_file" 2>/dev/null; then
        git -C "$destination" apply --reverse "$patch_file"
    else
        echo "error: $destination contains unexpected local changes" >&2
        exit 1
    fi
fi

if ! git -C "$destination" cat-file -e "$revision^{commit}" 2>/dev/null; then
    git -C "$destination" fetch --depth 1 origin "$revision"
fi

git -C "$destination" checkout -q --detach "$revision"
git -C "$destination" apply --check "$patch_file"
git -C "$destination" apply "$patch_file"

actual="$(git -C "$destination" rev-parse HEAD)"
if [[ "$actual" != "$revision" || ! -f "$destination/include/tesla.hpp" ]]; then
    echo "error: libtesla verification failed (expected $revision, got $actual)" >&2
    exit 1
fi

echo "libtesla verified at $revision with SWOTS lifecycle hook"
