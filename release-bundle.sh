#!/bin/bash
set -e
VER=$(grep 'GAME_VERSION' ~/cold-start-nx/source/constants.h | grep -oP '"[^"]+"' | tr -d '"')
ROOT=~/cold-start-nx
OUT=$ROOT/release-out
MINGW=/mnt/c/msys64/mingw64/bin
mkdir -p $OUT

echo "==> Linux bundle..."
bash $ROOT/build-pc/bundle.sh
cd $ROOT/build-pc/dist
zip -r $OUT/cold_start-linux-$VER.zip . -x '*.log'
echo "Linux done: cold_start-linux-$VER.zip"

echo "==> Windows bundle..."
WIN_STAGE=$(mktemp -d)
cp $ROOT/build-win/cold_start.exe $WIN_STAGE/
cp -r $ROOT/romfs $WIN_STAGE/
# Bundle all DLLs needed on a clean Windows install (transitively: HarfBuzz, Brotli, OpenSSL…)
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
    libminiupnpc.dll; do
    [ -f "$MINGW/$dll" ] && cp "$MINGW/$dll" $WIN_STAGE/
done
# Also include any DLLs the build dropped next to the exe
cp $ROOT/build-win/*.dll $WIN_STAGE/ 2>/dev/null || true
zip -r $OUT/cold_start-windows-$VER.zip -j $WIN_STAGE
rm -rf $WIN_STAGE
echo "Windows done: cold_start-windows-$VER.zip"

echo "==> Switch..."
cp $ROOT/cold_start.nro $OUT/cold-start-nx.nro
echo "Switch done: cold-start-nx.nro"

echo "==> Android..."
APK=$(ls $ROOT/android/app/build/outputs/apk/release/cold_start-android-*.apk 2>/dev/null | tail -1)
if [ -z "$APK" ]; then
    APK=$ROOT/android/app/build/outputs/apk/debug/app-debug.apk
fi
cp "$APK" $OUT/cold_start-android-$VER.apk
echo "Android done: cold_start-android-$VER.apk"

echo ""
echo "==> release-out/ contents:"
ls -lh $OUT
