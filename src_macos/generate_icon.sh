#!/bin/zsh
# Generate AppIcon.icns from a high-resolution source image using sips and iconutil

set -e

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <source_image_path> <output_icns_path>"
    exit 1
fi

SRC_IMG="$1"
OUT_ICNS="$2"

if [ ! -f "$SRC_IMG" ]; then
    echo "Error: Source image '$SRC_IMG' not found."
    exit 1
fi

echo "Generating iconset directory..."
ICONSET_DIR="AppIcon.iconset"
mkdir -p "$ICONSET_DIR"

# Resize helper function using sips
resize_icon() {
    local size="$1"
    local name="$2"
    echo "Resizing to ${size}x${size} -> ${name}..."
    sips -s format png -z "$size" "$size" "$SRC_IMG" --out "$ICONSET_DIR/$name" > /dev/null
}

# Generate all standard Apple Icon Sizes
resize_icon 16     "icon_16x16.png"
resize_icon 32     "icon_16x16@2x.png"
resize_icon 32     "icon_32x32.png"
resize_icon 64     "icon_32x32@2x.png"
resize_icon 128    "icon_128x128.png"
resize_icon 256    "icon_128x128@2x.png"
resize_icon 256    "icon_256x256.png"
resize_icon 512    "icon_256x256@2x.png"
resize_icon 512    "icon_512x512.png"
resize_icon 1024   "icon_512x512@2x.png"

echo "Compiling iconset into ICNS format..."
iconutil -c icns "$ICONSET_DIR" -o "$OUT_ICNS"

echo "Cleaning up temporary iconset..."
rm -rf "$ICONSET_DIR"

echo "Success! Packed AppIcon.icns to: $OUT_ICNS"
