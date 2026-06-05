#!/bin/bash
set -e
ROOT=/mnt/z/cold-start-nx
VER=$(grep 'GAME_VERSION' $ROOT/source/constants.h | grep -oP '"[^"]+"' | tr -d '"')
OUT=$ROOT/release-out
STAGE=$(mktemp -d)
MINGW=/mnt/c/msys64/mingw64/bin

mkdir -p "$OUT"

cp "$ROOT/build-win/cold_start.exe" "$STAGE/"
cp -r "$ROOT/romfs" "$STAGE/romfs"

for dll in \
    libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll \
    SDL2.dll SDL2_image.dll SDL2_mixer.dll SDL2_ttf.dll \
    libjpeg-8.dll libpng16-16.dll zlib1.dll \
    libwebp-7.dll libwebpdecoder-3.dll libwebpdemux-2.dll libwebpmux-3.dll \
    libogg-0.dll libvorbis-0.dll libvorbisfile-3.dll \
    libfreetype-6.dll libcurl-4.dll miniupnpc.dll; do
    if [ -f "$MINGW/$dll" ]; then
        cp "$MINGW/$dll" "$STAGE/"
        echo "  + $dll"
    else
        echo "  WARN: $dll not found"
    fi
done
cp "$ROOT/build-win/"*.dll "$STAGE/" 2>/dev/null || true

ZIP="$OUT/cold_start-windows-$VER.zip"
rm -f "$ZIP"
cd "$STAGE" && zip -r "$ZIP" .
rm -rf "$STAGE"
echo "Done: $ZIP ($(du -sh $ZIP | cut -f1))"
