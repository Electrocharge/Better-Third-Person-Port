#!/usr/bin/env bash

set -euo pipefail

if [ "$#" -lt 2 ]; then
    echo "Usage: $0 <mod_name> <build_dir>" >&2
    exit 1
fi

MOD_NAME="$1"
BUILD_DIR="$2"

if ! command -v zip >/dev/null 2>&1; then
    echo "Error: 'zip' command not found. Please install it first (e.g. apt-get install -y zip)." >&2
    exit 1
fi

SO_FILE=$(find "$BUILD_DIR" -name "lib${MOD_NAME}.so" | head -n1)

if [ -z "$SO_FILE" ]; then
    echo "Error: lib${MOD_NAME}.so was not found under '$BUILD_DIR'." >&2
    echo "Make sure the project has been built (xmake -y) and that MOD_NAME matches xmake.lua." >&2
    exit 1
fi

OUT_DIR="$(dirname "$SO_FILE")"
PACK_DIR="$OUT_DIR/pack"
PACK_FILE="$OUT_DIR/${MOD_NAME}.levipack"

rm -rf "$PACK_DIR" "$PACK_FILE"
mkdir -p "$PACK_DIR"

# Copy the mod library and manifest to the package root.
cp "$SO_FILE" "$PACK_DIR/lib${MOD_NAME}.so"
cp manifest.json "$PACK_DIR/manifest.json"

# Copy the contents of 'files/' directly into the package root.
if [ -d files ]; then
    cp -r files/. "$PACK_DIR/"
fi

(
    cd "$PACK_DIR"
    zip -r -q "../$(basename "$PACK_FILE")" .
)

echo "Mod .so     : $SO_FILE"
echo "Mod package : $PACK_FILE"

if [ -n "${GITHUB_OUTPUT:-}" ]; then
    {
        echo "so_file=$SO_FILE"
        echo "pack_file=$PACK_FILE"
    } >> "$GITHUB_OUTPUT"
fi