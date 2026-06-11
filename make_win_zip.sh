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

# Auto-detect DLLs actually needed by the exe (requires objdump in PATH)
if command -v objdump >/dev/null 2>&1 && [ -f "$ROOT/build-win/cold_start.exe" ]; then
    AUTO_DLLS=$(objdump -p "$ROOT/build-win/cold_start.exe" 2>/dev/null | awk '/DLL Name:/{print $3}')
else
    AUTO_DLLS=""
fi

# Explicit list covers DLLs pulled in transitively (SDL2_ttf→HarfBuzz→Graphite2,
# FreeType→Brotli; SDL2_mixer→mpg123/opus/FLAC; libcurl→OpenSSL/libssh2).
for dll in \
    libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll \
    SDL2.dll SDL2_image.dll SDL2_mixer.dll SDL2_ttf.dll \
    libjpeg-8.dll libpng16-16.dll zlib1.dll libbz2-1.dll \
    libwebp-7.dll libwebpdecoder-3.dll libwebpdemux-2.dll libwebpmux-3.dll \
    libogg-0.dll libvorbis-0.dll libvorbisfile-3.dll \
    libopus-0.dll libopusfile-0.dll \
    libmpg123-0.dll \
    libFLAC-8.dll libFLAC-12.dll \
    libmodplug-1.dll \
    libfreetype-6.dll \
    libharfbuzz-0.dll libgraphite2.dll \
    libbrotlidec.dll libbrotlicommon.dll \
    libcurl-4.dll \
    libssl-3.dll libcrypto-3.dll libssl-1_1-x64.dll libcrypto-1_1-x64.dll \
    libssh2-1.dll \
    libminiupnpc.dll \
    $AUTO_DLLS; do
    [ -z "$dll" ] && continue
    if [ -f "$MINGW/$dll" ]; then
        cp "$MINGW/$dll" "$STAGE/"
        echo "  + $dll"
    fi
done
cp "$ROOT/build-win/"*.dll "$STAGE/" 2>/dev/null || true

ZIP="$OUT/cold_start-windows-$VER.zip"
rm -f "$ZIP"
cd "$STAGE" && zip -r "$ZIP" .
rm -rf "$STAGE"
echo "Done: $ZIP ($(du -sh $ZIP | cut -f1))"
