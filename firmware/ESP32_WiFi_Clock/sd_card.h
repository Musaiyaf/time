#pragma once
#include <Arduino.h>

// Thin wrapper around the ESP32 SD library for an optional SD card module
// on its own dedicated SPI bus (config.h SD_SCLK_PIN/SD_MISO_PIN/
// SD_MOSI_PIN/SD_CS_PIN) - lets the on-device menu browse its contents.
//
// Entirely optional hardware: begin() just probes for a card, and every
// other call here is a safe no-op / empty result if none was found.
namespace SdCard {

// Mounts the card (if present) on its own SPI bus. Safe to call even with
// no card inserted or module wired up. Call once from setup().
void begin();

// True if a card was found and mounted by begin().
bool isPresent();

// One directory entry, filled in by listDir().
struct Entry {
  String name; // base name only, e.g. "photo.jpg" - never a full path
  bool isDir;
  uint32_t size; // bytes; 0 for directories
};

// Lists up to maxEntries entries of the directory at path (e.g. "/",
// "/sub") into out, returning how many were found (capped at maxEntries).
// Returns 0 if there's no card, or the path doesn't exist / isn't a
// directory.
int listDir(const String &path, Entry *out, int maxEntries);

// Reads a whole small text file (e.g. a Custom Face's face.cfg) into a
// String. Returns "" if there's no card or the file doesn't exist -
// callers can't tell that apart from a genuinely empty file, which is
// fine for the key=value config files this is meant for.
String readTextFile(const String &path);

// Reads a raw RGB565 image - width*height native-endian uint16_t pixel
// values, no header, row-major - from path into the caller-allocated out
// buffer (must hold at least width*height uint16_t's). Returns false
// (leaving out untouched) if there's no card, the file's missing, or
// it's shorter than expected. Used for Custom Face backgrounds.
bool readImage(const String &path, uint16_t *out, int width, int height);

} // namespace SdCard
