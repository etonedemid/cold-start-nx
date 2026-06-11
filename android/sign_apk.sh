#!/bin/bash
SDK=/mnt/c/Android/Sdk/build-tools/34.0.0
ROOT=~/cold-start-nx
VER=$(grep 'GAME_VERSION' $ROOT/source/constants.h | grep -oP '"[^"]+"' | tr -d '"')
APK_IN=$ROOT/android/app/build/outputs/apk/release/app-release-unsigned.apk
APK_ALIGNED=/tmp/app-release-aligned.apk
APK_OUT=${1:-$ROOT/release-out/cold_start-android-$VER.apk}

$SDK/zipalign -v -p 4 "$APK_IN" "$APK_ALIGNED" 2>&1 | tail -2
$SDK/apksigner sign --ks /root/.android/debug.keystore --ks-pass pass:android --key-pass pass:android --out "$APK_OUT" "$APK_ALIGNED" 2>&1
echo "Result: $(ls -lh $APK_OUT 2>/dev/null)"
