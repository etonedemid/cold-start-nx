#!/bin/sh
# ─── Download SDL2 sources for Android build ─────────────────────────────────
# Run this once before building:  cd android && ./setup_sdl.sh
# ─────────────────────────────────────────────────────────────────────────────
set -e

SDL2_VER="2.30.10"
SDL2_IMAGE_VER="2.8.4"
SDL2_TTF_VER="2.22.0"
SDL2_MIXER_VER="2.8.0"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
JNI_DIR="$SCRIPT_DIR/app/jni"
mkdir -p "$JNI_DIR"

download_extract() {
    url="$1"; dest="$2"; strip="$3"
    if [ -d "$dest" ]; then
        echo "  Already present: $(basename "$dest")"
        return
    fi
    echo "  Downloading $(basename "$dest")..."
    curl -sL "$url" | tar xz -C "$JNI_DIR"
    mv "$JNI_DIR/$strip" "$dest"
}

echo "Setting up SDL2 sources for Android build..."

download_extract \
    "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VER}/SDL2-${SDL2_VER}.tar.gz" \
    "$JNI_DIR/SDL2" \
    "SDL2-${SDL2_VER}"

download_extract \
    "https://github.com/libsdl-org/SDL_image/releases/download/release-${SDL2_IMAGE_VER}/SDL2_image-${SDL2_IMAGE_VER}.tar.gz" \
    "$JNI_DIR/SDL2_image" \
    "SDL2_image-${SDL2_IMAGE_VER}"

download_extract \
    "https://github.com/libsdl-org/SDL_ttf/releases/download/release-${SDL2_TTF_VER}/SDL2_ttf-${SDL2_TTF_VER}.tar.gz" \
    "$JNI_DIR/SDL2_ttf" \
    "SDL2_ttf-${SDL2_TTF_VER}"

download_extract \
    "https://github.com/libsdl-org/SDL_mixer/releases/download/release-${SDL2_MIXER_VER}/SDL2_mixer-${SDL2_MIXER_VER}.tar.gz" \
    "$JNI_DIR/SDL2_mixer" \
    "SDL2_mixer-${SDL2_MIXER_VER}"

echo "SDL2 sources ready in $JNI_DIR/"
echo "Now build with:  cd android && gradle assembleDebug"
