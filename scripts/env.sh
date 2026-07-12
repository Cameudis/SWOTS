#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Source this to set up the devkitPro environment on Fedora (where the pacman
# package doesn't install a profile.d script). Put a line like
#   source /home/y2/Projects/switch/scripts/env.sh
# in your ~/.zshrc / ~/.bashrc, or just `source scripts/env.sh` before building.
export DEVKITPRO=/opt/devkitpro
export DEVKIT_A64=$DEVKITPRO/devkitA64
export PORTLIBS_PATH=$DEVKITPRO/portlibs
# devkitARM/devkitPPC are harmless extras if installed; keep them if set.
: "${DEVKITARM:=/opt/devkitpro/devkitARM}"; export DEVKITARM
: "${DEVKITPPC:=/opt/devkitpro/devkitPPC}"; export DEVKITPPC
