#!/bin/bash
# Detect Android SDK build-tools.
if [ -n "$ANDROID_HOME" ]; then
    SDK_BASE="$ANDROID_HOME"
elif [ -d "$HOME/Android/Sdk" ]; then
    SDK_BASE="$HOME/Android/Sdk"
elif [ -d "/mnt/c/Android/Sdk" ]; then
    SDK_BASE="/mnt/c/Android/Sdk"
else
    echo "ERROR: ANDROID_HOME not set and SDK not found." >&2; exit 1
fi

SDK=$(ls -d "$SDK_BASE/build-tools/"* 2>/dev/null | sort -V | tail -1)
if [ -z "$SDK" ]; then
    echo "ERROR: No Android build-tools found in $SDK_BASE/build-tools/" >&2; exit 1
fi

ROOT=~/cold-start-nx
VER=$(grep 'GAME_VERSION' $ROOT/source/constants.h | grep -oP '"[^"]+"' | tr -d '"')
APK_IN=$ROOT/android/app/build/outputs/apk/release/app-release-unsigned.apk
APK_ALIGNED=/tmp/app-release-aligned.apk
APK_OUT=${1:-$ROOT/release-out/cold_start-android-$VER.apk}

# Use debug keystore (auto-created by Android tools at first use).
KEYSTORE="${ANDROID_KEYSTORE:-$HOME/.android/debug.keystore}"
if [ ! -f "$KEYSTORE" ]; then
    mkdir -p "$HOME/.android"
    keytool -genkeypair -v -keystore "$KEYSTORE" -alias androiddebugkey \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -storepass android -keypass android \
        -dname "CN=Android Debug,O=Android,C=US" 2>/dev/null
fi

"$SDK/zipalign" -v -p 4 "$APK_IN" "$APK_ALIGNED" 2>&1 | tail -2
"$SDK/apksigner" sign --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android \
    --out "$APK_OUT" "$APK_ALIGNED" 2>&1
echo "Result: $(ls -lh $APK_OUT 2>/dev/null)"
