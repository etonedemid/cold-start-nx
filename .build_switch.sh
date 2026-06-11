#!/bin/sh
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitA64
export PATH=$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$DEVKITPRO/portlibs/switch/bin:$PATH
cd ~/cold-start-nx
make -j$(nproc)
