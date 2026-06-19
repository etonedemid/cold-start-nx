#!/bin/sh
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=$DEVKITPRO/devkitPPC
export PATH=$DEVKITPRO/tools/bin:$DEVKITPRO/devkitPPC/bin:$DEVKITPRO/portlibs/wiiu/bin:$PATH
cd ~/cold-start-nx
make -f Makefile.wiiu -j$(nproc)
