#!/usr/bin/env python3
"""Build a Custom Face package for the ESP32 WiFi grid clock's SD card.

Takes a background image and produces the two files the firmware's Custom
Face reads at /faces/custom/ on the SD card:

  bg.bin    - the image, resized to 320x140 and converted to raw RGB565
              pixels (no header), which the firmware streams straight into
              a display buffer.
  face.cfg  - a small text file with the digit/accent colours.

Usage:
    pip install Pillow
    python3 make_custom_face.py background.png --out out_dir \
        --digit-color "#00FFFF" --accent-color "#FF4FA3"

Then copy out_dir's contents onto the SD card as /faces/custom/
(i.e. the card should end up with /faces/custom/bg.bin and
/faces/custom/face.cfg).

Background sizing: 320x140 is the clock digit area only (the 320x30
status bar at the top is always drawn as solid colour pill badges, so
that strip of the source image would never be visible anyway). If your
image isn't already that shape, it's stretched to fit - crop/pad it
yourself first if you want to avoid distortion.
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


def parse_hex_color(s):
    s = s.strip()
    if not s.startswith("#") or len(s) != 7:
        raise argparse.ArgumentTypeError(f"expected a #RRGGBB colour, got {s!r}")
    return s.upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", help="background image (any format Pillow can read: PNG, JPG, ...)")
    parser.add_argument("--out", default="custom_face_out", help="output directory (default: custom_face_out)")
    parser.add_argument("--digit-color", type=parse_hex_color, default="#FFFFFF",
                         help="clock digit colour, #RRGGBB (default white)")
    parser.add_argument("--accent-color", type=parse_hex_color, default="#00E5FF",
                         help="status badge text colour, #RRGGBB (default cyan)")
    args = parser.parse_args()

    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow is required: pip install Pillow")

    src = Image.open(args.image).convert("RGB")
    if src.size != (FACE_WIDTH, FACE_HEIGHT):
        print(f"Resizing {src.size[0]}x{src.size[1]} -> {FACE_WIDTH}x{FACE_HEIGHT} "
              f"(stretched, not cropped - pre-crop the source if that distorts it)")
        src = src.resize((FACE_WIDTH, FACE_HEIGHT), Image.LANCZOS)

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    bg_path = out_dir / "bg.bin"
    pixels = src.load()
    with open(bg_path, "wb") as f:
        for y in range(FACE_HEIGHT):
            row = bytearray(FACE_WIDTH * 2)
            for x in range(FACE_WIDTH):
                r, g, b = pixels[x, y]
                # Little-endian: matches how the ESP32 (a little-endian CPU)
                # lays a uint16_t out in memory, which is what the firmware
                # reads this file into directly.
                struct.pack_into("<H", row, x * 2, rgb565(r, g, b))
            f.write(row)
    print(f"Wrote {bg_path} ({bg_path.stat().st_size} bytes)")

    cfg_path = out_dir / "face.cfg"
    cfg_path.write_text(
        "# Custom Face config - key=value, one per line, '#' comments.\n"
        f"digit_color={args.digit_color}\n"
        f"accent_color={args.accent_color}\n"
    )
    print(f"Wrote {cfg_path}")

    print(f"\nCopy the contents of {out_dir}/ onto the SD card as /faces/custom/ "
          f"(so the card has /faces/custom/bg.bin and /faces/custom/face.cfg), "
          f"then cycle the clock face (LEFT/RIGHT) to Custom on the device.")


if __name__ == "__main__":
    main()
