#!/bin/sh
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
export PATH=$DEVKITPRO/tools/bin:$DEVKITPRO/devkitPPC/bin:$DEVKITPRO/portlibs/wiiu/bin:$PATH
# Use COLD_START_ROOT if set, otherwise fall back to repo root
ROOT="${COLD_START_ROOT:-$(dirname "$0")}"
cd "$ROOT"
# Switch and Wii U both link cold_start.elf in the repo root; drop any stale
# (possibly AArch64) elf so make relinks a fresh PowerPC one.
rm -f cold_start.elf
make -f Makefile.wiiu -j$(nproc)
