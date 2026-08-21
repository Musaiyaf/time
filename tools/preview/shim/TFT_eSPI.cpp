#include <TFT_eSPI.h>

// The built-in 5x7 "font 1", from the installed TFT_eSPI - same data the
// device uses for the compact rows.
#include <glcdfont.c>

TFT_eSPI::TFT_eSPI(int w, int h) : w_(w), h_(h) { buf_.assign((size_t)w * h, 0); }

void TFT_eSPI::init() {}
void TFT_eSPI::setRotation(uint8_t) {}

// ---- primitives ---------------------------------------------------------
void TFT_eSPI::drawPixel(int32_t x, int32_t y, uint32_t color) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return; // clipped, as on the panel
  buf_[(size_t)y * w_ + x] = (uint16_t)color;
}

uint16_t TFT_eSPI::readPixel(int32_t x, int32_t y) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
  return buf_[(size_t)y * w_ + x];
}

void TFT_eSPI::fillScreen(uint32_t color) { fillRect(0, 0, w_, h_, color); }

void TFT_eSPI::fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
  for (int32_t j = 0; j < h; j++)
    for (int32_t i = 0; i < w; i++) drawPixel(x + i, y + j, color);
}

void TFT_eSPI::drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
  drawFastHLine(x, y, w, color);
  drawFastHLine(x, y + h - 1, w, color);
  drawFastVLine(x, y, h, color);
  drawFastVLine(x + w - 1, y, h, color);
}

void TFT_eSPI::drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t color) {
  for (int32_t i = 0; i < w; i++) drawPixel(x + i, y, color);
}

void TFT_eSPI::drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t color) {
  for (int32_t i = 0; i < h; i++) drawPixel(x, y + i, color);
}

void TFT_eSPI::drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
  int32_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int32_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int32_t err = dx + dy;
  while (true) {
    drawPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int32_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void TFT_eSPI::drawCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color) {
  int32_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
  drawPixel(x0, y0 + r, color);
  drawPixel(x0, y0 - r, color);
  drawPixel(x0 + r, y0, color);
  drawPixel(x0 - r, y0, color);
  while (x < y) {
    if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
    x++; ddF_x += 2; f += ddF_x;
    drawPixel(x0 + x, y0 + y, color); drawPixel(x0 - x, y0 + y, color);
    drawPixel(x0 + x, y0 - y, color); drawPixel(x0 - x, y0 - y, color);
    drawPixel(x0 + y, y0 + x, color); drawPixel(x0 - y, y0 + x, color);
    drawPixel(x0 + y, y0 - x, color); drawPixel(x0 - y, y0 - x, color);
  }
}

void TFT_eSPI::fillCircle(int32_t x0, int32_t y0, int32_t r, uint32_t color) {
  for (int32_t y = -r; y <= r; y++)
    for (int32_t x = -r; x <= r; x++)
      if (x * x + y * y <= r * r) drawPixel(x0 + x, y0 + y, color);
}

// Approximated with a hard edge rather than TFT_eSPI's anti-aliasing: the
// difference is a pixel of softness at the rim, which never changes
// whether something fits or collides - the thing this preview exists to
// check.
void TFT_eSPI::fillSmoothCircle(int32_t x, int32_t y, int32_t r, uint32_t color, uint32_t) {
  fillCircle(x, y, r, color);
}

void TFT_eSPI::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                              uint32_t color) {
  drawFastHLine(x + r, y, w - 2 * r, color);
  drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
  drawFastVLine(x, y + r, h - 2 * r, color);
  drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
  int32_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, cx = 0, cy = r;
  while (cx < cy) {
    if (f >= 0) { cy--; ddF_y += 2; f += ddF_y; }
    cx++; ddF_x += 2; f += ddF_x;
    drawPixel(x + w - r + cx - 1, y + r - cy, color);
    drawPixel(x + r - cx, y + r - cy, color);
    drawPixel(x + w - r + cy - 1, y + r - cx, color);
    drawPixel(x + r - cy, y + r - cx, color);
    drawPixel(x + w - r + cx - 1, y + h - r + cy - 1, color);
    drawPixel(x + r - cx, y + h - r + cy - 1, color);
    drawPixel(x + w - r + cy - 1, y + h - r + cx - 1, color);
    drawPixel(x + r - cy, y + h - r + cx - 1, color);
  }
}

void TFT_eSPI::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                              uint32_t color) {
  fillRect(x + r, y, w - 2 * r, h, color);
  for (int32_t dy = 0; dy < h; dy++) {
    for (int32_t dx = 0; dx < r; dx++) {
      int32_t ox = r - dx;
      int32_t oy = (dy < r) ? r - dy : (dy >= h - r ? dy - (h - r) + 1 : 0);
      if (ox * ox + oy * oy <= r * r) {
        drawPixel(x + dx, y + dy, color);
        drawPixel(x + w - 1 - dx, y + dy, color);
      }
    }
  }
}

void TFT_eSPI::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2,
                             int32_t y2, uint32_t color) {
  int32_t minx = min(x0, min(x1, x2)), maxx = max(x0, max(x1, x2));
  int32_t miny = min(y0, min(y1, y2)), maxy = max(y0, max(y1, y2));
  auto edge = [](int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t px, int32_t py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
  };
  for (int32_t py = miny; py <= maxy; py++) {
    for (int32_t px = minx; px <= maxx; px++) {
      int32_t w0 = edge(x1, y1, x2, y2, px, py);
      int32_t w1 = edge(x2, y2, x0, y0, px, py);
      int32_t w2 = edge(x0, y0, x1, y1, px, py);
      if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
        drawPixel(px, py, color);
    }
  }
}

// ---- text ---------------------------------------------------------------
void TFT_eSPI::setTextColor(uint16_t c) { textcolor_ = textbgcolor_ = c; }
void TFT_eSPI::setTextColor(uint16_t c, uint16_t b) { textcolor_ = c; textbgcolor_ = b; }
void TFT_eSPI::setTextDatum(uint8_t d) { datum_ = d; }
void TFT_eSPI::setTextFont(uint8_t f) { textfont_ = f; gfxFont_ = nullptr; }

// Mirrors TFT_eSPI::setFreeFont(): the largest above- and below-baseline
// offsets across the font's glyphs, which the datum maths below needs.
void TFT_eSPI::setFreeFont(const GFXfont *f) {
  if (!f) { setTextFont(1); return; }
  textfont_ = 1;
  gfxFont_ = f;
  glyphAscent_ = 0;
  glyphDescent_ = 0;
  uint16_t numChars = f->last - f->first;
  for (uint16_t c = 0; c < numChars; c++) {
    int8_t ab = (int8_t)-f->glyph[c].yOffset;
    if (ab > (int8_t)glyphAscent_) glyphAscent_ = ab;
    int8_t bb = (int8_t)(f->glyph[c].height - ab);
    if (bb > (int8_t)glyphDescent_) glyphDescent_ = bb;
  }
}

// Smooth (.vlw) fonts drive the big clock-face digits. Rendering them
// would mean reimplementing TFT_eSPI's whole VLW decoder; until that
// earns its keep, loading one is recorded so a caller can tell the
// preview can't speak for that screen, rather than quietly drawing
// nothing and looking authoritative.
void TFT_eSPI::loadFont(const uint8_t *) { smoothFont_ = true; }
void TFT_eSPI::unloadFont() { smoothFont_ = false; }

int16_t TFT_eSPI::textWidth(const char *s) {
  int32_t w = 0;
  if (gfxFont_) {
    for (const char *p = s; *p; p++) {
      uint16_t c = (uint8_t)*p;
      if (c < gfxFont_->first || c > gfxFont_->last) continue;
      const GFXglyph &g = gfxFont_->glyph[c - gfxFont_->first];
      // Last character uses offset+width, which can exceed xAdvance.
      w += p[1] ? g.xAdvance : (g.xOffset + g.width);
    }
  } else {
    for (const char *p = s; *p; p++) w += 6; // built-in 5x7 plus spacing
  }
  return (int16_t)w;
}
int16_t TFT_eSPI::textWidth(const String &s) { return textWidth(s.c_str()); }

int16_t TFT_eSPI::fontHeight() { return gfxFont_ ? gfxFont_->yAdvance : 8; }

void TFT_eSPI::drawGlyph(uint16_t c, int32_t x, int32_t y) {
  if (!gfxFont_) { // built-in 5x7, column-major, 6px advance
    if (c > 255) return;
    for (int8_t i = 0; i < 5; i++) {
      uint8_t col = pgm_read_byte(&font[c * 5 + i]);
      for (int8_t j = 0; j < 8; j++) {
        bool on = col & (1 << j);
        if (on) drawPixel(x + i, y + j, textcolor_);
        else if (textcolor_ != textbgcolor_) drawPixel(x + i, y + j, textbgcolor_);
      }
    }
    if (textcolor_ != textbgcolor_)
      for (int8_t j = 0; j < 8; j++) drawPixel(x + 5, y + j, textbgcolor_);
    return;
  }
  if (c < gfxFont_->first || c > gfxFont_->last) return;
  const GFXglyph &g = gfxFont_->glyph[c - gfxFont_->first];
  const uint8_t *bitmap = gfxFont_->bitmap + g.bitmapOffset;
  uint16_t bit = 0;
  for (uint8_t yy = 0; yy < g.height; yy++) {
    for (uint8_t xx = 0; xx < g.width; xx++, bit++) {
      if (bitmap[bit >> 3] & (0x80 >> (bit & 7)))
        drawPixel(x + g.xOffset + xx, y + g.yOffset + yy, textcolor_);
    }
  }
}

// A transcription of TFT_eSPI::drawString()'s positioning. The
// background rectangle in the free-font branch is not cosmetic: it is
// what makes one line of text visibly erase another it overlaps, which
// is exactly the class of bug this harness is meant to catch.
int16_t TFT_eSPI::drawString(const char *s, int32_t poX, int32_t poY) {
  int16_t cwidth = textWidth(s);
  int16_t cheight = 8;

  if (gfxFont_) {
    cheight = glyphAscent_;
    poY += cheight; // free fonts are positioned from the baseline
    if (datum_ == BL_DATUM || datum_ == BC_DATUM || datum_ == BR_DATUM) cheight += glyphDescent_;
  }

  switch (datum_) {
    case TC_DATUM: poX -= cwidth / 2; break;
    case TR_DATUM: poX -= cwidth; break;
    case ML_DATUM: poY -= cheight / 2; break;
    case MC_DATUM: poX -= cwidth / 2; poY -= cheight / 2; break;
    case MR_DATUM: poX -= cwidth; poY -= cheight / 2; break;
    case BL_DATUM: poY -= cheight; break;
    case BC_DATUM: poX -= cwidth / 2; poY -= cheight; break;
    case BR_DATUM: poX -= cwidth; poY -= cheight; break;
    default: break;
  }

  if (gfxFont_ && textcolor_ != textbgcolor_) {
    int16_t bgH = glyphAscent_ + glyphDescent_;
    int8_t xo = 0;
    if (s[0] >= gfxFont_->first && s[0] <= gfxFont_->last)
      xo = gfxFont_->glyph[(uint8_t)s[0] - gfxFont_->first].xOffset;
    int16_t bgW = cwidth;
    if (xo > 0) xo = 0; else bgW -= xo;
    fillRect(poX + xo, poY - glyphAscent_, bgW, bgH, textbgcolor_);
  }

  int32_t sumX = 0;
  for (const char *p = s; *p; p++) {
    uint16_t c = (uint8_t)*p;
    drawGlyph(c, poX + sumX, poY);
    if (gfxFont_) {
      if (c >= gfxFont_->first && c <= gfxFont_->last)
        sumX += gfxFont_->glyph[c - gfxFont_->first].xAdvance;
    } else {
      sumX += 6;
    }
  }
  return cwidth;
}
int16_t TFT_eSPI::drawString(const String &s, int32_t x, int32_t y) {
  return drawString(s.c_str(), x, y);
}

// ---- sprite -------------------------------------------------------------
void *TFT_eSprite::createSprite(int16_t w, int16_t h) {
  w_ = w;
  h_ = h;
  buf_.assign((size_t)w * h, 0);
  return buf_.data();
}

void TFT_eSprite::deleteSprite() { buf_.clear(); w_ = h_ = 0; }

void TFT_eSprite::pushSprite(int32_t x, int32_t y) {
  if (!parent_) return;
  for (int32_t j = 0; j < h_; j++)
    for (int32_t i = 0; i < w_; i++)
      parent_->drawPixel(x + i, y + j, buf_[(size_t)j * w_ + i]);
}
