#!/usr/bin/env bash
# Push all release artifacts to itch.io via butler.
# Usage: ./butler-push.sh [version]
# Example: ./butler-push.sh v1.6.0
set -e

GAME="etonedemid/coldstart"
VER="${1:-$(grep 'GAME_VERSION' source/constants.h | grep -o '"[^"]*"' | tr -d '"')}"
DIR="release-out"
BUTLER="${BUTLER:-tools/butler/butler}"

if [ ! -f "$DIR/cold_start-linux-${VER}.zip" ] && [ ! -f "$DIR/cold_start-windows-${VER}.zip" ]; then
    echo "No artifacts found in $DIR/ for version $VER"
    echo "Run the build first, or pass the version as an argument."
    exit 1
fi

push() {
    local file="$1" channel="$2"
    if [ -f "$file" ]; then
        echo "→ pushing $channel"
        "$BUTLER" push "$file" "$GAME:$channel" --userversion "$VER"
    fi
}

push "$DIR/cold_start-linux-${VER}.zip"        linux
push "$DIR/cold_start-linux-server-${VER}.zip" linux-server
push "$DIR/cold_start-windows-${VER}.zip"      windows
push "$DIR/cold-start-nx.nro"                  switch

echo ""
echo "Done! https://etonedemid.itch.io/coldstart"
