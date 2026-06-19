#!/bin/sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitA64
export PATH=$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$DEVKITPRO/portlibs/switch/bin:$PATH
cd ~/cold-start-nx
# Switch and Wii U both link cold_start.elf in the repo root; remove any stale
# (possibly PowerPC) elf so make relinks a fresh AArch64 one instead of feeding
# the wrong-arch elf to elf2nro ("Invalid ELF: expected AArch64!").
rm -f cold_start.elf
make -j$(nproc)
