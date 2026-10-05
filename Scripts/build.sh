#!/bin/bash
#
#  build.sh - configure and build Kitbox in Release, then verify it.
#
#  "It builds" and "it measures correctly" are the same command here: a drum
#  sampler whose filter is 3 dB off or whose hits land on block boundaries
#  compiles perfectly well.
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

cd "$PROJECT_DIR"

cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"

echo
echo "Measuring the engine:"
echo
"$BUILD_DIR/KitboxCheck_artefacts/Release/KitboxCheck"

echo
echo "Checking the processor and rendering the panel:"
echo
"$BUILD_DIR/EditorShot_artefacts/Release/EditorShot" "$BUILD_DIR/shots"

echo
echo "Checking the saved kit with macOS's own property-list parser:"
plutil -lint "$BUILD_DIR/shots/Demo Kit.aupreset"
plutil -p "$BUILD_DIR/shots/Demo Kit.aupreset" | grep -E '^  "(manufacturer|name|subtype|type|version)"'

echo
echo "Built:"
ls -d "$BUILD_DIR/Kitbox_artefacts/Release/"*/*.{vst3,component,app} 2>/dev/null || true
echo "Panel: $BUILD_DIR/shots/kitbox.png"
