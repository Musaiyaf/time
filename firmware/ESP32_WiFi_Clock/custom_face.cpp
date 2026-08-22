#include "custom_face.h"
#include "sd_card.h"
#include <Preferences.h>

namespace {

const char *NVS_NAMESPACE = "clockcfg";
const char *NVS_KEY = "customFacePath";

const char MAGIC[4] = {'C', 'F', 'C', '1'};
const int NAME_LEN = 28; // 27 chars + NUL - must match the design tool's export

// Order the file stores glyphs in, and the order glyphFor() looks them up
// by - must match tools/make_custom_face.html's export function exactly.
const char GLYPH_CHARS[11] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':'};
int glyphIndex(char ch) {
  for (int i = 0; i < 11; i++) {
    if (GLYPH_CHARS[i] == ch) return i;
  }
  return -1;
}

uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

String activePathVal;
String activeNameVal;
bool activeFlag = false;
uint16_t bgColorVal = 0, badgeBgVal = 0, badgeFgVal = 0;
bool hasBgFlag = false;

// One heap allocation backs the background (if any) plus every glyph's
// pixels, back to back - simpler lifetime than 12 separate allocations,
// and each Glyph/backgroundPixels() just points somewhere inside it.
uint8_t *assetBuf = nullptr;
const uint16_t *bgPixelsVal = nullptr;
CustomFace::Glyph glyphSlots[11];

void freeAssets() {
  if (assetBuf) {
    free(assetBuf);
    assetBuf = nullptr;
  }
  bgPixelsVal = nullptr;
  for (int i = 0; i < 11; i++) glyphSlots[i] = CustomFace::Glyph();
  hasBgFlag = false;
}

const size_t HEADER_LEN = 4 + NAME_LEN + 2 + 2 + 2 + 1;

} // namespace

namespace CustomFace {

const int BG_W = 320;
const int BG_H = 140;

int list(Entry *out, int maxEntries) {
  if (!SdCard::isPresent()) return 0;

  SdCard::Entry raw[40];
  int rawCount = SdCard::listDir("/faces", raw, 40);
  int count = 0;
  for (int i = 0; i < rawCount && count < maxEntries; i++) {
    if (raw[i].isDir) continue;
    String lower = raw[i].name;
    lower.toLowerCase();
    if (!lower.endsWith(".cface")) continue;

    String path = "/faces/" + raw[i].name;

    // Peek just the embedded display name out of the header rather than a
    // full load - this runs once per file every time the picker opens, so
    // a full parse+RAM-cache just to build a menu would be wasteful.
    uint8_t header[4 + NAME_LEN];
    size_t got = SdCard::readAt(path, 0, header, sizeof(header));
    String displayName = raw[i].name;
    if (got == sizeof(header) && memcmp(header, MAGIC, 4) == 0) {
      char nameBuf[NAME_LEN + 1] = {0};
      memcpy(nameBuf, header + 4, NAME_LEN);
      String embedded = String(nameBuf);
      embedded.trim();
      if (embedded.length()) displayName = embedded;
    }

    out[count].path = path;
    out[count].displayName = displayName;
    count++;
  }
  return count;
}

bool setActive(const String &path) {
  if (!SdCard::isPresent()) return false;

  uint8_t header[HEADER_LEN];
  if (SdCard::readAt(path, 0, header, HEADER_LEN) != HEADER_LEN) return false;
  if (memcmp(header, MAGIC, 4) != 0) return false;

  char nameBuf[NAME_LEN + 1] = {0};
  memcpy(nameBuf, header + 4, NAME_LEN);
  String name = String(nameBuf);
  name.trim();

  size_t off = 4 + NAME_LEN;
  uint16_t bgColorV = rd16(header + off); off += 2;
  uint16_t badgeBgV = rd16(header + off); off += 2;
  uint16_t badgeFgV = rd16(header + off); off += 2;
  bool hasBgV = header[off] != 0;

  if (!SdCard::openSeqRead(path)) return false;

  // Every glyph stores its own w/h right before its pixels, so the total
  // size (and thus how big one allocation needs to be) isn't known until
  // this first pass reads all 11 headers back to back.
  size_t fileOff = HEADER_LEN + (hasBgV ? (size_t)BG_W * BG_H * 2 : 0);
  int glyphW[11], glyphH[11];
  size_t glyphBytes[11];
  size_t totalGlyphBytes = 0;
  for (int i = 0; i < 11; i++) {
    uint8_t dims[4];
    if (SdCard::readSeqAt(fileOff, dims, 4) != 4) {
      SdCard::closeSeqRead();
      return false;
    }
    glyphW[i] = rd16(dims);
    glyphH[i] = rd16(dims + 2);
    glyphBytes[i] = (size_t)glyphW[i] * glyphH[i] * 2;
    totalGlyphBytes += glyphBytes[i];
    fileOff += 4 + glyphBytes[i];
  }

  size_t bgBytes = hasBgV ? (size_t)BG_W * BG_H * 2 : 0;
  size_t newBufSize = bgBytes + totalGlyphBytes;
  uint8_t *newBuf = (uint8_t *)ps_malloc(newBufSize);
  if (!newBuf) newBuf = (uint8_t *)malloc(newBufSize);
  if (!newBuf) {
    SdCard::closeSeqRead();
    return false;
  }

  size_t readOff = HEADER_LEN;
  size_t writeOff = 0;
  if (hasBgV) {
    if (SdCard::readSeqAt(readOff, newBuf, bgBytes) != bgBytes) {
      SdCard::closeSeqRead();
      free(newBuf);
      return false;
    }
    readOff += bgBytes;
    writeOff += bgBytes;
  }

  CustomFace::Glyph newGlyphs[11];
  for (int i = 0; i < 11; i++) {
    readOff += 4; // this glyph's own w/h header, already captured above
    if (SdCard::readSeqAt(readOff, newBuf + writeOff, glyphBytes[i]) != glyphBytes[i]) {
      SdCard::closeSeqRead();
      free(newBuf);
      return false;
    }
    newGlyphs[i].w = glyphW[i];
    newGlyphs[i].h = glyphH[i];
    newGlyphs[i].pixels = (const uint16_t *)(newBuf + writeOff);
    readOff += glyphBytes[i];
    writeOff += glyphBytes[i];
  }
  SdCard::closeSeqRead();

  freeAssets();
  assetBuf = newBuf;
  hasBgFlag = hasBgV;
  bgPixelsVal = hasBgV ? (const uint16_t *)assetBuf : nullptr;
  for (int i = 0; i < 11; i++) glyphSlots[i] = newGlyphs[i];

  bgColorVal = bgColorV;
  badgeBgVal = badgeBgV;
  badgeFgVal = badgeFgV;
  activePathVal = path;
  activeNameVal = name.length() ? name : path;
  activeFlag = true;

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString(NVS_KEY, path);
  prefs.end();
  return true;
}

void clearActive() {
  freeAssets();
  activeFlag = false;
  activePathVal = "";
  activeNameVal = "";

  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.remove(NVS_KEY);
  prefs.end();
}

bool hasActive() { return activeFlag; }
String activeName() { return activeNameVal; }
String activePath() { return activePathVal; }
uint16_t bgColor() { return bgColorVal; }
uint16_t badgeBg() { return badgeBgVal; }
uint16_t badgeFg() { return badgeFgVal; }
bool hasBackgroundImage() { return hasBgFlag; }
const uint16_t *backgroundPixels() { return bgPixelsVal; }

Glyph glyphFor(char ch) {
  int idx = glyphIndex(ch);
  return (idx >= 0) ? glyphSlots[idx] : Glyph();
}

void begin() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  String saved = prefs.getString(NVS_KEY, "");
  prefs.end();

  if (saved.length() && !setActive(saved)) {
    // The saved file is gone or unreadable (deleted via the web portal,
    // card swapped, ...) - fall back to "no active face" rather than
    // leaving a stale NVS pointer nothing will ever load again.
    clearActive();
  }
}

} // namespace CustomFace
