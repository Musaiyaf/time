#pragma once
#include <Arduino.h>

// Loads and renders "Custom Face" clock faces: fully user-designed digit
// artwork (one image each for 0-9 and the colon) plus an optional
// background image and status-badge colours, read from a .cface file on
// the SD card's /faces directory.
//
// Files are built entirely off-device by tools/make_custom_face.html (see
// that file's own header comment for the exact binary layout this reads -
// never hand-edit one) and get onto the card either through a real card
// reader or the web portal's Custom Faces upload (web_portal.cpp's
// /faces/upload).
//
// The SD card can hold as many .cface files as fit, but only one is
// "active" (loaded into RAM) at a time - picked from the on-device
// Settings > Custom Face list (see menu.cpp's runCustomFacePicker()). The
// active choice is remembered in NVS so it survives a reboot, and
// clock_display.cpp's FACE_CUSTOM shows whichever one that is.
namespace CustomFace {

// Restores whichever face was last selected, if any (SdCard::begin() must
// already have run). Self-heals to "no active face" if that file has
// since been deleted or renamed.
void begin();

struct Entry {
  String path;        // full SD path, e.g. "/faces/retro.cface"
  String displayName; // the face's own embedded name, shown in the picker
};

// Scans /faces for *.cface files, filling out (up to maxEntries) and
// returning how many were found. 0 if there's no SD card or no faces
// saved yet.
int list(Entry *out, int maxEntries);

// Loads path as the active face and saves the choice to NVS. Returns
// false (leaving whatever was active before untouched) if the file is
// missing, truncated, or fails its magic-number check.
bool setActive(const String &path);

// Clears the active face (FACE_CUSTOM falls back to its "pick one from
// Settings" placeholder) and forgets the saved NVS choice.
void clearActive();

bool hasActive();
String activeName();
String activePath();

uint16_t bgColor();
uint16_t badgeBg();
uint16_t badgeFg();
bool hasBackgroundImage();

// Fixed size every background image is authored at - matches the clock's
// own digit-row area (CLOCK_H in clock_display.cpp) - so there's no
// on-device scaling to do. The design tool exports at exactly this size.
extern const int BG_W;
extern const int BG_H;

// Raw pointer into the loaded background's RGB565 pixel buffer (BG_W x
// BG_H, row-major), or nullptr if hasBackgroundImage() is false.
const uint16_t *backgroundPixels();

// One digit/colon glyph's own native size, position nudge and RGB565
// pixel data. ch is '0'-'9' or ':'. pixels is null only if there's no
// active face at all - the design tool requires all 11 glyphs before it
// will export a file, so a successfully loaded face always has every one
// of them. dx/dy are a pixel offset from this glyph's auto-centred slot
// position (see drawCustomFaceRow() in clock_display.cpp) - 0,0 unless
// the face's author dragged it off-centre in the design tool.
struct Glyph {
  int w = 0, h = 0;
  int dx = 0, dy = 0;
  const uint16_t *pixels = nullptr;
};
Glyph glyphFor(char ch);

} // namespace CustomFace
