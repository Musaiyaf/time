#pragma once
#include <Arduino.h>

// Shared by every Photo-style digit set (PhotoDigits.h, GlassDigits.h,
// ...) so each one's array of 10 native-size RGB565 bitmaps has a common
// type clock_display.cpp can point at interchangeably.
struct PhotoDigit {
  const uint16_t *data;
  // Per-pixel alpha (0=transparent .. 255=opaque), same w x h layout as
  // data, or nullptr for a fully-opaque set (Photo's font-sampler digits,
  // which are only ever drawn on a flat background). Only Glass's digits
  // carry this, so the Glass face can alpha-blend over a wallpaper image
  // instead of a flat colour - see drawPhotoDigitToSprite().
  const uint8_t *alpha;
  uint8_t w, h;
};
