#!/usr/bin/env bash
# build_all.sh - Build Cold Start for all platforms and bundle into release-out/
# Platforms: Windows (via MinGW cross-compile), Linux, Switch (via devkitPro), Android, Wii U (via WUT)
# Usage: ./build_all.sh [--skip-win] [--skip-linux] [--skip-switch] [--skip-android] [--skip-wiiu]
# Run from the cold-start-nx directory.

set -euo pipefail

# ── Parse args ──────────────────────────────────────────────────────────────
SKIP_WIN=false
SKIP_LINUX=false
SKIP_SWITCH=false
SKIP_ANDROID=false
SKIP_WIIU=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-win)     SKIP_WIN=true;     shift ;;
    --skip-linux)   SKIP_LINUX=true;   shift ;;
    --skip-switch)  SKIP_SWITCH=true;  shift ;;
    --skip-android) SKIP_ANDROID=true; shift ;;
    --skip-wiiu)    SKIP_WIIU=true;    shift ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

# ── Config ──────────────────────────────────────────────────────────────────
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VER=$(grep -oP 'GAME_VERSION\s*=\s*"\K[^"]+' "$ROOT/source/constants.h")
OUT="$ROOT/release-out"
WIN_ZIP="$OUT/cold_start-windows-$VER.zip"
LIN_ZIP="$OUT/cold_start-linux-$VER.zip"
NRO_OUT="$OUT/cold-start-nx.nro"
APK_OUT="$OUT/cold_start-android-$VER.apk"
WUHB_OUT="$OUT/cold_start-wiiu-$VER.wuhb"

MINGW_DEPS="$HOME/mingw-deps"
SDL2_ROOT="$MINGW_DEPS/SDL2-2.30.11/x86_64-w64-mingw32"
SDL2_IMAGE_ROOT="$MINGW_DEPS/SDL2_image-2.8.2/x86_64-w64-mingw32"
SDL2_TTF_ROOT="$MINGW_DEPS/SDL2_ttf-2.24.0/x86_64-w64-mingw32"
SDL2_MIXER_ROOT="$MINGW_DEPS/SDL2_mixer-2.8.1/x86_64-w64-mingw32"
CURL_ROOT="$MINGW_DEPS/curl-build/curl-8.20.0_5-win64-mingw"
MINIUPNPC_ROOT="$MINGW_DEPS/miniupnpc-win64"
COMPAT_HEADER="$MINGW_DEPS/mingw_compat.h"

echo ""
echo -e "\e[37mCold Start $VER - All-Platform Build\e[0m"
echo -e "\e[90mOutput: $OUT\e[0m"
echo ""

mkdir -p "$OUT"

# ── Helpers ───────────────────────────────────────────────────────────────────
banner() { echo -e "\e[36m==> $1\e[0m"; }
ok()     { echo -e "\e[32m    OK: $1\e[0m"; }
fail()   { echo -e "\e[31m    FAIL: $1\e[0m"; exit 1; }

zip_dir() {
    local src="$1" out="$2"
    rm -f "$out"
    (cd "$src" && zip -r "$out" .)
    local sz
    sz=$(du -h "$out" | cut -f1)
    ok "$out ($sz)"
}

# ── Windows (MinGW cross-compile) ─────────────────────────────────────────────
if [[ "$SKIP_WIN" == false ]]; then
    banner "Building Windows (MinGW)..."

    BUILD_WIN="$ROOT/build-win"

    if [[ ! -f "$BUILD_WIN/CMakeCache.txt" ]] || [[ "$ROOT/CMakeLists.txt" -nt "$BUILD_WIN/CMakeCache.txt" ]]; then
        rm -rf "$BUILD_WIN"
        cmake -S "$ROOT" -B "$BUILD_WIN" \
            -DCMAKE_TOOLCHAIN_FILE="$ROOT/toolchain-windows.cmake" \
            -DCMAKE_BUILD_TYPE=Release \
            -DSDL2_DIR="$SDL2_ROOT/lib/cmake/SDL2" \
            -DSDL2_image_DIR="$SDL2_IMAGE_ROOT/lib/cmake/SDL2_image" \
            -DSDL2_ttf_DIR="$SDL2_TTF_ROOT/lib/cmake/SDL2_ttf" \
            -DSDL2_mixer_DIR="$SDL2_MIXER_ROOT/lib/cmake/SDL2_mixer" \
            -DCURL_INCLUDE_DIR="$CURL_ROOT/include" \
            -DCURL_LIBRARY="$CURL_ROOT/lib/libcurl.a" \
            -DMINIUPNPC_INCLUDE_DIR="$MINIUPNPC_ROOT/include" \
            -DMINIUPNPC_LIBRARY="$MINIUPNPC_ROOT/lib/libminiupnpc.a" \
            -DCMAKE_INCLUDE_PATH="$SDL2_ROOT/include;$SDL2_IMAGE_ROOT/include;$SDL2_TTF_ROOT/include;$SDL2_MIXER_ROOT/include" \
            -DCMAKE_CXX_FLAGS="-include $COMPAT_HEADER -D_WIN32_WINNT=0x0600 -I$SDL2_ROOT/include -I$SDL2_IMAGE_ROOT/include -I$SDL2_TTF_ROOT/include -I$SDL2_MIXER_ROOT/include" \
            -DCMAKE_C_FLAGS="-include $COMPAT_HEADER -D_WIN32_WINNT=0x0600" \
            -DCMAKE_EXE_LINKER_FLAGS="-L$CURL_ROOT/lib -L$MINIUPNPC_ROOT/lib"
    fi

    cmake --build "$BUILD_WIN" -- -j"$(nproc)"

    # Collect: exe + romfs + DLLs
    winStage="$(mktemp -d /tmp/cs_win_stage.XXXXXX)"
    cp "$BUILD_WIN/cold_start.exe" "$winStage/"
    cp -r "$ROOT/romfs" "$winStage/romfs"

    # MinGW runtime DLLs (from system cross toolchain)
    MINGW_BIN="/usr/x86_64-w64-mingw32/bin"
    runtimeDlls=(
        "libgcc_s_seh-1.dll"
        "libstdc++-6.dll"
        "libwinpthread-1.dll"
    )
    for dll in "${runtimeDlls[@]}"; do
        src="$MINGW_BIN/$dll"
        if [[ -f "$src" ]]; then
            cp "$src" "$winStage/"
        else
            echo -e "\e[33m    WARN: $dll not found at $src (skipping)\e[0m"
        fi
    done

    # SDL2 DLLs (from your mingw-deps builds)
    sdlDllDirs=(
        "$SDL2_ROOT/bin"
        "$SDL2_IMAGE_ROOT/bin"
        "$SDL2_TTF_ROOT/bin"
        "$SDL2_MIXER_ROOT/bin"
    )
    for dir in "${sdlDllDirs[@]}"; do
        for dll in "$dir"/*.dll; do
            [[ -f "$dll" ]] && cp "$dll" "$winStage/"
        done
    done

    # curl DLL if present (dynamic build)
    for dll in "$CURL_ROOT/bin"/*.dll; do
        [[ -f "$dll" ]] && cp "$dll" "$winStage/"
    done

    # Any DLLs cmake placed next to the exe
    for dll in "$BUILD_WIN"/*.dll; do
        [[ -f "$dll" ]] && cp "$dll" "$winStage/"
    done

    zip_dir "$winStage" "$WIN_ZIP"
    rm -rf "$winStage"
fi

# ── Linux ─────────────────────────────────────────────────────────────────────
if [[ "$SKIP_LINUX" == false ]]; then
    banner "Building Linux..."

    cmake --build "$ROOT/build-pc" -- -j"$(nproc)"

    bash "$ROOT/build-pc/bundle.sh"

    find "$ROOT/build-pc/dist" -name '*.log' -delete

    if [[ -d "$ROOT/build-pc/dist" ]]; then
        rm -f "$LIN_ZIP"
        (cd "$ROOT/build-pc/dist" && zip -r "$LIN_ZIP" .)
        sz=$(du -h "$LIN_ZIP" | cut -f1)
        ok "$LIN_ZIP ($sz)"
    else
        fail "dist folder not found at $ROOT/build-pc/dist"
    fi
fi

# ── Switch ────────────────────────────────────────────────────────────────────
if [[ "$SKIP_SWITCH" == false ]]; then
    banner "Building Switch NRO..."

    bash "$ROOT/.build_switch.sh"

    nroSrc="$ROOT/cold_start.nro"
    [[ -f "$nroSrc" ]] || fail "cold_start.nro not found after Switch build"
    cp "$nroSrc" "$NRO_OUT"
    sz=$(du -h "$NRO_OUT" | cut -f1)
    ok "$NRO_OUT ($sz)"
fi

# ── Android ───────────────────────────────────────────────────────────────────
if [[ "$SKIP_ANDROID" == false ]]; then
    # Auto-detect Android SDK; skip gracefully if not installed.
    _ANDROID_SDK=""
    for _d in "$HOME/Android/Sdk" "/opt/android-sdk" "/mnt/c/Android/Sdk"; do
        [[ -d "$_d" ]] && { _ANDROID_SDK="$_d"; break; }
    done

    if [[ -z "$_ANDROID_SDK" ]]; then
        echo -e "\e[33m    SKIP: Android SDK not found (set ANDROID_HOME or install to ~/Android/Sdk)\e[0m"
    else
        export ANDROID_HOME="$_ANDROID_SDK"
        # Update local.properties so Gradle picks up the right SDK path.
        sed -i "s|^sdk\.dir=.*|sdk.dir=$_ANDROID_SDK|" "$ROOT/android/local.properties"

        banner "Building Android APK (SDK: $_ANDROID_SDK)..."
        bash "$ROOT/.build_android.sh"
        bash "$ROOT/android/sign_apk.sh" "$APK_OUT"
        [[ -f "$APK_OUT" ]] || fail "Signed APK not found at $APK_OUT"
        sz=$(du -h "$APK_OUT" | cut -f1)
        ok "$APK_OUT ($sz)"
    fi
fi

# ── Wii U ────────────────────────────────────────────────────────────────────
if [[ "$SKIP_WIIU" == false ]]; then
    if [[ ! -d /opt/devkitpro/wut ]]; then
        echo -e "\e[33m    SKIP: WUT not found at /opt/devkitpro/wut (install via dkp-pacman: wiiu-dev)\e[0m"
    else
        banner "Building Wii U WUHB..."

        bash "$ROOT/.build_wiiu.sh"

        wuhbSrc="$ROOT/cold_start.wuhb"
        [[ -f "$wuhbSrc" ]] || fail "cold_start.wuhb not found after Wii U build"
        cp "$wuhbSrc" "$WUHB_OUT"
        sz=$(du -h "$WUHB_OUT" | cut -f1)
        ok "$WUHB_OUT ($sz)"
    fi
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo -e "\e[37mrelease-out/ contents:\e[0m"
for f in "$OUT"/*; do
    [[ -f "$f" ]] || continue
    sz=$(du -h "$f" | cut -f1)
    printf "  %-45s %6s\n" "$(basename "$f")" "$sz"
done
echo ""
echo -e "\e[32mDone. Cold Start $VER ready for release.\e[0m"