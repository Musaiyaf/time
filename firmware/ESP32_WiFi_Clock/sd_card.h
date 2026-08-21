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

// Reads up to len raw bytes starting at byte offset within path into out.
// Opens and closes the file on every call, so it's only good for one-off
// reads (e.g. Video Face's header parsing) - repeated per-frame playback
// reads should use openSeqRead()/readSeqAt() instead, which pay that
// open/close cost once for the whole session rather than every frame.
size_t readAt(const String &path, size_t offset, uint8_t *out, size_t len);

// Opens path once for many subsequent readSeqAt() calls (e.g. Video Face
// reading one frame per tick for as long as that face stays open),
// avoiding the repeated open/seek/close overhead readAt() pays on every
// single call. Only one such read session can be open at a time,
// independent of the beginWrite()/writeChunk()/endWrite() write session.
// Returns false if there's no card or the file doesn't exist.
bool openSeqRead(const String &path);
size_t readSeqAt(size_t offset, uint8_t *out, size_t len);
void closeSeqRead();

bool exists(const String &path);

// Deletes a file. Returns true if it no longer exists afterwards (this
// includes the case where it never existed).
bool remove(const String &path);

// Streaming write for large uploads (e.g. Video Face's saved video) that
// don't fit in RAM as a single buffer: beginWrite() opens/truncates path
// (creating one level of parent directory if needed - SD.mkdir() isn't
// recursive, and every caller here only ever nests one level deep),
// writeChunk() appends, endWrite() closes. Only one write can be open at
// a time; a second beginWrite() before endWrite() fails.
bool beginWrite(const String &path);
bool writeChunk(const uint8_t *data, size_t len);
void endWrite();

} // namespace SdCard
