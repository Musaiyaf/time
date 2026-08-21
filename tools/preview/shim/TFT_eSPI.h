#pragma once
// A TFT_eSPI stand-in that draws into an RGB565 framebuffer in memory
// instead of over SPI, so a screen can be rendered and looked at on a
// normal computer.
//
// The point is fidelity, not convenience: the text layout here
// (textWidth, the datum offsets, the free-font baseline adjustment and
// the background rectangle drawString paints when a background colour is
// set) is a deliberate transcription of TFT_eSPI's own implementation.
// If it drifts from the real library, the preview starts lying, which
// would be worse than having no preview - see tools/preview/README.md.
//
// Only the calls firmware/ actually makes are implemented. Anything
// missing is a compile error rather than a silent no-op, on purpose.

#include <Arduino.h>
#include <vector>

// Adafruit GFX font structures, matching TFT_eSPI's Fonts/GFXFF/gfxfont.h
// so the library's own font headers can be included unmodified.
typedef struct {
  uint32_t bitmapOffset;
  uint8_t width, height;
  uint8_t xAdvance;
  int8_t xOffset, yOffset;
} GFXglyph;

typedef struct {
  uint8_t *bitmap;
  GFXglyph *glyph;
  uint16_t first, last;
  uint8_t yAdvance;
} GFXfont;

// The three free fonts the firmware uses, taken from the installed
// TFT_eSPI so the glyph data is identical to the device's.
#include <FreeSansBold12pt7b.h>
#include <FreeSansBold18pt7b.h>
#include <FreeSansBold9pt7b.h>

// Text datums (values match TFT_eSPI).
#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#define TFT_RED 0xF800
#define TFT_GREEN 0x07E0
#define TFT_BLUE 0x001F
#define TFT_CYAN 0x07FF
#define TFT_YELLOW 0xFFE0

class TFT_eSPI {
 public:
  TFT_eSPI(int w = 320, int h = 170);
  virtual ~TFT_eSPI() {}

  void init();
  void setRotation(uint8_t r);

  // Geometry
  virtual void drawPixel(int32_t x, int32_t y, uint32_t color);
  virtual uint16_t readPixel(int32_t x, int32_t y);
  void fillScreen(uint32_t color);
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
  void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color);
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t color);
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color);
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color);
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
  void drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color);
  void fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color);
  void fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t color, uint32_t bg);
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                    uint32_t color);

  // Text
  void setTextColor(uint16_t c);
  void setTextColor(uint16_t c, uint16_t b);
  void setTextDatum(uint8_t d);
  void setFreeFont(const GFXfont *f);
  void setTextFont(uint8_t f);
  int16_t textWidth(const String &s);
  int16_t textWidth(const char *s);
  int16_t fontHeight();
  int16_t drawString(const String &s, int32_t x, int32_t y);
  int16_t drawString(const char *s, int32_t x, int32_t y);

  // Smooth (.vlw) fonts are not emulated - see loadFont() in the .cpp.
  void loadFont(const uint8_t *font);
  void unloadFont();

  static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }

  int32_t width() const { return w_; }
  int32_t height() const { return h_; }

  // Host-only: hand the framebuffer to the PNG writer.
  const uint16_t *frameBuffer() const { return buf_.data(); }
  bool smoothFontActive() const { return smoothFont_; }

 protected:
  void drawGlyph(uint16_t c, int32_t x, int32_t y);
  int32_t w_, h_;
  std::vector<uint16_t> buf_;

  const GFXfont *gfxFont_ = nullptr;
  uint8_t glyphAscent_ = 0, glyphDescent_ = 0;
  uint8_t textfont_ = 1;
  uint8_t datum_ = TL_DATUM;
  uint16_t textcolor_ = 0xFFFF, textbgcolor_ = 0xFFFF;
  bool smoothFont_ = false;
};

// A sprite is just a second framebuffer that can be blitted onto a
// parent, which is exactly how it behaves on the device.
class TFT_eSprite : public TFT_eSPI {
 public:
  explicit TFT_eSprite(TFT_eSPI *parent) : TFT_eSPI(0, 0), parent_(parent) {}
  void setColorDepth(int8_t bits) { (void)bits; }
  void *createSprite(int16_t w, int16_t h);
  void deleteSprite();
  void fillSprite(uint32_t color) { fillScreen(color); }
  void pushSprite(int32_t x, int32_t y);

 private:
  TFT_eSPI *parent_;
};
