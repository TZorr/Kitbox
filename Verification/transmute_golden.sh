#!/bin/bash
#
# Verification/transmute_golden.sh
# Kitbox
#
# Builds TransmuteGolden against Transmute's own Swift engine and writes its
# reference output, which KitboxCheck compares the C++ port with (the
# "Transmute port" section). Optimised (-O): unoptimised, a single fit takes
# minutes.
#
# Usage: Verification/transmute_golden.sh [samples folder] [output folder]
#   defaults: ../Transmute/Test Samples, build/transmute-golden
#

set -euo pipefail
cd "$(dirname "$0")/.."

TRANSMUTE="${TRANSMUTE:-../Transmute/Transmute}"
SAMPLES="${1:-../Transmute/Test Samples}"
OUTPUT="${2:-build/transmute-golden}"
BINARY="${TMPDIR:-/tmp}/transmute_golden"

SOURCES=()
while IFS= read -r -d '' file; do SOURCES+=("$file"); done \
    < <(find "$TRANSMUTE/Transmute/Engine" -name '*.swift' -print0 | sort -z)

swiftc -O -default-isolation MainActor -swift-version 5 \
    -o "$BINARY" Verification/TransmuteGolden/main.swift "${SOURCES[@]}"

rm -rf "$OUTPUT"
"$BINARY" "$SAMPLES" "$OUTPUT"
