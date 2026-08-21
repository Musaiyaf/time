#pragma once
#include <Arduino.h>

// Shared by every Photo-style digit set (PhotoDigits.h, BotanicalDigits.h,
// ...) so each one's array of 10 native-size RGB565 bitmaps has a common
// type clock_display.cpp can point at interchangeably.
struct PhotoDigit {
  const uint16_t *data;
  uint8_t w, h;
};
