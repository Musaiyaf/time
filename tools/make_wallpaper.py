#!/usr/bin/env python3
"""Build a Glass Face wallpaper image for the ESP32 WiFi grid clock's SD card.

Converts any photo into the raw RGB565 .bin format the Glass face's
on-device wallpaper picker reads (hold LEFT from the clock face to browse
and pick one - see the README's Glass Face Wallpaper section). Same pixel
format/shape as Custom Face's bg.bin: 320x140, the clock digit area only
(the 30px status bar at the top is always solid-colour badges, so that
strip of the source image would never show anyway).

Usage:
    pip install Pillow
    python3 make_wallpaper.py sunset.jpg --out sunset.bin

Then copy the .bin file(s) into a /wallpapers/ folder on the SD card (any
filenames, any subfolders - the on-device picker just browses whatever's
there) and pick one on the clock: hold LEFT from the Glass face.
"""
import argparse
import struct
import sys
from pathlib import Path

FACE_WIDTH = 320
FACE_HEIGHT = 140  # clock digit area only - see module docstring


def rgb565(r, g, b):
    # Same bit packing the firmware uses (menu.cpp's rgb565(), TFT_eSPI's
    # color565()) - must match exactly or colours will come out wrong.
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", help="wallpaper image (any format Pillow can read: PNG, JPG, ...)")
    parser.add_argument("--out", default="wallpaper.bin", help="output .bin path (default: wallpaper.bin)")
    parser.add_argument("--fit", choices=["stretch", "cover"], default="cover",
                         help="stretch: distort to exactly 320x140; "
                              "cover: scale to fill and centre-crop, no distortion (default)")
    args = parser.parse_args()

    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow is required: pip install Pillow")

    src = Image.open(args.image).convert("RGB")

    if args.fit == "stretch":
        if src.size != (FACE_WIDTH, FACE_HEIGHT):
            print(f"Resizing {src.size[0]}x{src.size[1]} -> {FACE_WIDTH}x{FACE_HEIGHT} (stretched)")
        src = src.resize((FACE_WIDTH, FACE_HEIGHT), Image.LANCZOS)
    else:
        sw, sh = src.size
        scale = max(FACE_WIDTH / sw, FACE_HEIGHT / sh)
        rw, rh = max(FACE_WIDTH, round(sw * scale)), max(FACE_HEIGHT, round(sh * scale))
        print(f"Scaling {sw}x{sh} -> {rw}x{rh}, then centre-cropping to {FACE_WIDTH}x{FACE_HEIGHT}")
        src = src.resize((rw, rh), Image.LANCZOS)
        left = (rw - FACE_WIDTH) // 2
        top = (rh - FACE_HEIGHT) // 2
        src = src.crop((left, top, left + FACE_WIDTH, top + FACE_HEIGHT))

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    pixels = src.load()
    with open(out_path, "wb") as f:
        for y in range(FACE_HEIGHT):
            row = bytearray(FACE_WIDTH * 2)
            for x in range(FACE_WIDTH):
                r, g, b = pixels[x, y]
                # Little-endian: matches how the ESP32 (a little-endian CPU)
                # lays a uint16_t out in memory, which is what the firmware
                # reads this file into directly.
                struct.pack_into("<H", row, x * 2, rgb565(r, g, b))
            f.write(row)
    print(f"Wrote {out_path} ({out_path.stat().st_size} bytes)")
    print(f"\nCopy {out_path.name} into /wallpapers/ on the SD card, then on the "
          f"clock: cycle (LEFT/RIGHT) to the Glass face, hold LEFT to open the "
          f"wallpaper picker, and pick it.")


if __name__ == "__main__":
    main()
