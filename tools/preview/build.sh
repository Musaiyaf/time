#!/bin/sh
# Builds and runs the render preview - see tools/preview/README.md.
# Usage: tools/preview/build.sh [scene ...]
set -e

cd "$(dirname "$0")/../.."   # repo root
FW=firmware/ESP32_WiFi_Clock
OUT=tools/preview/out
mkdir -p "$OUT"

g++ -std=c++17 -O1 -Wall \
  -DHOST_PREVIEW \
  -I tools/preview/shim \
  -I tools/preview/third_party/gfxff_fonts \
  -I "$FW" \
  tools/preview/main.cpp \
  tools/preview/shim/TFT_eSPI.cpp \
  tools/preview/shim/host_runtime.cpp \
  tools/preview/shim/host_fakes.cpp \
  "$FW/menu.cpp" \
  -o "$OUT/preview"

"$OUT/preview" "$@"
