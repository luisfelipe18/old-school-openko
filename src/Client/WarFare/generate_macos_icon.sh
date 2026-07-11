#!/bin/sh
# Generates a multi-resolution KnightOnLine.icns from a square source PNG
# (docs/PORT_POSIX_PLAN.md, F8). Invoked by CMake's POST_BUILD step on macOS.
#
# Usage: generate_macos_icon.sh <source.png> <iconset-dir> <output.icns>
#
# Many AI-generated portrait images ship with a decorative frame/border
# painted into the pixels themselves (not something macOS adds), which makes
# the subject look small and "boxed in" once composed into a Dock icon. This
# script center-crops ~12% off each edge (a ~24% zoom) before building the
# iconset, trimming that baked-in border so the subject fills the icon.
# Adjust CROP_PERCENT below if a particular source image needs more/less.

set -e

SRC="$1"
ICONSET="$2"
OUT_ICNS="$3"

CROP_PERCENT=12

WIDTH=$(sips -g pixelWidth "$SRC" | awk '/pixelWidth/{print $2}')
HEIGHT=$(sips -g pixelHeight "$SRC" | awk '/pixelHeight/{print $2}')

# Center-crop to (100 - 2*CROP_PERCENT)% of each dimension - sips -c crops
# around the image center by default.
CROP_W=$((WIDTH * (100 - CROP_PERCENT * 2) / 100))
CROP_H=$((HEIGHT * (100 - CROP_PERCENT * 2) / 100))

CROPPED="$ICONSET.cropped.png"
rm -rf "$ICONSET" "$CROPPED"
mkdir -p "$ICONSET"

sips -c "$CROP_H" "$CROP_W" "$SRC" --out "$CROPPED" >/dev/null

# Standard 10-entry size ladder a Retina .icns needs.
sips -z 16 16     "$CROPPED" --out "$ICONSET/icon_16x16.png"      >/dev/null
sips -z 32 32     "$CROPPED" --out "$ICONSET/icon_16x16@2x.png"   >/dev/null
sips -z 32 32     "$CROPPED" --out "$ICONSET/icon_32x32.png"      >/dev/null
sips -z 64 64     "$CROPPED" --out "$ICONSET/icon_32x32@2x.png"   >/dev/null
sips -z 128 128   "$CROPPED" --out "$ICONSET/icon_128x128.png"    >/dev/null
sips -z 256 256   "$CROPPED" --out "$ICONSET/icon_128x128@2x.png" >/dev/null
sips -z 256 256   "$CROPPED" --out "$ICONSET/icon_256x256.png"    >/dev/null
sips -z 512 512   "$CROPPED" --out "$ICONSET/icon_256x256@2x.png" >/dev/null
sips -z 512 512   "$CROPPED" --out "$ICONSET/icon_512x512.png"    >/dev/null
sips -z 1024 1024 "$CROPPED" --out "$ICONSET/icon_512x512@2x.png" >/dev/null

iconutil -c icns "$ICONSET" -o "$OUT_ICNS"
rm -rf "$ICONSET" "$CROPPED"

echo "Generated $OUT_ICNS from $SRC (cropped ${CROP_PERCENT}% per edge)"
