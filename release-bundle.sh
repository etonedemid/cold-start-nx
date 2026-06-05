#!/bin/bash
set -e
VER=2.3.1
ROOT=/mnt/z/cold-start-nx
OUT=$ROOT/release-out
mkdir -p $OUT

echo "==> Linux bundle..."
bash $ROOT/build-pc/bundle.sh
cd $ROOT/build-pc/dist
zip -r $OUT/cold_start-linux-$VER.zip . -x '*.log'
echo "Linux done: cold_start-linux-$VER.zip"

echo "==> Windows bundle..."
cd $ROOT/build-win
zip -r $OUT/cold_start-windows-$VER.zip cold_start.exe romfs mods *.dll -x '*.log' 2>/dev/null || \
zip -r $OUT/cold_start-windows-$VER.zip cold_start.exe romfs *.dll
echo "Windows done: cold_start-windows-$VER.zip"

echo "==> Switch..."
cp $ROOT/cold_start.nro $OUT/cold-start-nx.nro
echo "Switch done: cold-start-nx.nro"

echo "==> Android..."
cp $ROOT/android/app/build/outputs/apk/debug/app-debug.apk $OUT/cold_start-android-$VER.apk
echo "Android done: cold_start-android-$VER.apk"

echo ""
echo "==> release-out/ contents:"
ls -lh $OUT
