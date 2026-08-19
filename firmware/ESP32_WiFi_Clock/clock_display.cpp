#include "clock_display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include "FredokaDigits92.h"
// TFT_eSPI.h (with LOAD_GFXFF enabled) already pulls in every Adafruit GFX
// free font, including these two, via its own Fonts/GFXFF/gfxfont.h. Those
// font headers have no include guards, so including them again here would
// cause duplicate-definition build errors - just use the fonts directly.

namespace {

TFT_eSPI tft = TFT_eSPI();

// Reusable offscreen buffers, sized exactly to what they draw and created
// once at startup (never deleted/recreated) so a clock left running for
// weeks doesn't churn the heap. Redrawn only when their content actually
// changes, so the display never has to flicker-clear the whole panel.
TFT_eSprite digitSpr(&tft);
TFT_eSprite colonSpr(&tft);
TFT_eSprite yearSpr(&tft);
TFT_eSprite mdaySpr(&tft);
TFT_eSprite weekSpr(&tft);
TFT_eSprite doySpr(&tft);
TFT_eSprite wifiSpr(&tft);

// ---- Theme colours (approximating the reference photo) -------------
const uint16_t COL_BG        = TFT_BLACK;
const uint16_t COL_GRID      = tft.color565(55, 60, 68);
const uint16_t COL_COLON     = TFT_WHITE;
// One vivid colour per digit cell (HH:MM:SS -> cells 0,1, 3,4, 6,7; the
// colon cells 2 and 5 are unused here), rainbow-style like the reference.
const uint16_t COL_DIGIT_PALETTE[8] = {
  tft.color565(255, 79, 163),  // H tens   - pink
  tft.color565(255, 159, 28),  // H units  - orange
  0,                           // (colon, unused)
  tft.color565(155, 93, 229),  // M tens   - purple
  tft.color565(46, 204, 113),  // M units  - green
  0,                           // (colon, unused)
  tft.color565(255, 210, 63),  // S tens   - yellow
  tft.color565(61, 58, 237),   // S units  - blue
};

// ---- Status bar badges -------------------------------------------------
// Each badge is a separated "pill": a solid colour rectangle with rounded
// corners and a small gap to its neighbours, matching the reference photo.
const uint16_t COL_YEAR_BG   = tft.color565(27, 111, 209);   // blue
const uint16_t COL_YEAR_TXT  = tft.color565(15, 18, 26);     // near-black
const uint16_t COL_MONTH_BG  = tft.color565(224, 34, 45);    // red
const uint16_t COL_MONTH_TXT = TFT_WHITE;
const uint16_t COL_DAY_BG    = TFT_WHITE;
const uint16_t COL_DAY_TXT   = tft.color565(20, 20, 20);
const uint16_t COL_WEEK_BG   = tft.color565(255, 205, 30);   // yellow
const uint16_t COL_WEEK_TXT  = tft.color565(35, 28, 10);      // near-black
const uint16_t COL_DOY_BG    = tft.color565(224, 133, 45);
const uint16_t COL_WIFI_BG   = tft.color565(196, 238, 242);  // pastel cyan
const uint16_t COL_WIFI_ICON = tft.color565(25, 60, 80);     // dark on light bg
const uint16_t COL_WIFI_BAD  = tft.color565(214, 74, 74);
const uint16_t COL_WIFI_BAD_ICON = TFT_WHITE;
const uint16_t COL_BADGE_TXT = TFT_WHITE;

// ---- Layout -----------------------------------------------------------
const int SCR_W = TFT_SCREEN_WIDTH;
const int SCR_H = TFT_SCREEN_HEIGHT;

const int TOPBAR_H = 30;

// Pill geometry: each badge sprite is filled solid, then its 4 outer
// corners are carved back to the (black) background to round them off,
// with a few pixels of black gap left between neighbouring badges.
const int BADGE_MARGIN_Y = 3;
const int BADGE_RADIUS   = 5;

struct Badge { int x, w; uint16_t color; };
const Badge B_YEAR  = {3,   50, COL_YEAR_BG};
const Badge B_MDAY  = {56,  74, COL_MONTH_BG};   // month+day, two-tone
const Badge B_WEEK  = {133, 76, COL_WEEK_BG};
const Badge B_DOY   = {212, 76, COL_DOY_BG};
const Badge B_WIFI  = {291, 26, COL_WIFI_BG};

const int CLOCK_TOP = TOPBAR_H;
const int CLOCK_H   = SCR_H - TOPBAR_H;

// The clock face is HH:MM:SS -> 6 digit cells + 2 (narrower) colon cells.
// Digit cells are sized to fit the FredokaDigits92 smooth font (see
// FredokaDigits92.h - regenerate that file if this width changes; the
// font's widest glyphs are 52px, 2px more than the cell, so they clip by
// about 1px per side - not noticeable at this size); colon cells hold two
// small blinking dots. The two widths add up to exactly SCR_W (320):
// 50*6 + 10*2 = 320.
const int CELL_COUNT = 8;
const int CELL_DIGIT_W = 50;
const int CELL_COLON_W = 10;
const int COL_W[CELL_COUNT] = {
  CELL_DIGIT_W, CELL_DIGIT_W, CELL_COLON_W,
  CELL_DIGIT_W, CELL_DIGIT_W, CELL_COLON_W,
  CELL_DIGIT_W, CELL_DIGIT_W,
};

int colX(int col) {
  int x = 0;
  for (int i = 0; i < col; i++) x += COL_W[i];
  return x;
}

// ---- clock faces --------------------------------------------------------
// Tapping the BOOT button cycles between these (see ESP32_WiFi_Clock.ino).
// The status badge row is shared by every face; only the big HH:MM:SS area
// (and whether it gets the dashed grid lines) changes.
enum ClockFaceId { FACE_RAINBOW_GRID = 0, FACE_RETRO_FLIP = 1, FACE_COUNT = 2 };
int currentFace = FACE_RAINBOW_GRID;

// ---- state cache, so we only repaint what changed --------------------
char lastDigit[CELL_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
bool gridDrawn = false;
String lastDateStr = "\x01";       // year badge cache
String lastMonthDayStr = "\x01";   // month/day badge cache
String lastWeekStr = "\x01";
int lastYday = -999;
bool lastWifiConnected = true; // force first draw
int lastWifiBars = -1;
int lastColonVisible = -1; // -1 = not drawn yet, forces first draw

bool isColonCell(int col) { return col == 2 || col == 5; }

void drawGrid() {
  tft.fillRect(0, CLOCK_TOP, SCR_W, CLOCK_H, COL_BG);
  if (currentFace != FACE_RAINBOW_GRID) return; // other faces: plain black
  int x = 0;
  for (int i = 0; i <= CELL_COUNT; i++) {
    // dashed vertical separator
    for (int y = CLOCK_TOP + 4; y < SCR_H - 4; y += 6) {
      tft.drawFastVLine(x == SCR_W ? x - 1 : x, y, 3, COL_GRID);
    }
    // small tick marks top & bottom, like grid intersections
    int xx = (x == SCR_W) ? x - 2 : x;
    tft.fillRect(xx, CLOCK_TOP, 2, 2, COL_GRID);
    tft.fillRect(xx, SCR_H - 2, 2, 2, COL_GRID);
    if (i < CELL_COUNT) x += COL_W[i];
  }
}

// Brightens the top portion of a just-drawn glyph towards white, fading
// back to its normal colour by GLOSS_FRAC of the way down - a glossy
// highlight like the top-lit look in the reference photo. Only touches
// pixels the glyph actually painted (background stays pure black).
const float GLOSS_FRAC     = 0.45f; // how far down the gloss extends
const float GLOSS_STRENGTH = 0.55f; // how far towards white at the very top

void applyDigitGloss(TFT_eSprite &spr, int w, int h) {
  int gradH = (int)(h * GLOSS_FRAC);
  for (int y = 0; y < gradH; y++) {
    float t = GLOSS_STRENGTH * (float)(gradH - y) / gradH;
    for (int x = 0; x < w; x++) {
      uint16_t px = spr.readPixel(x, y);
      if (px == 0) continue; // pure background - leave untouched
      uint8_t r = (px >> 11) & 0x1F;
      uint8_t g = (px >> 5) & 0x3F;
      uint8_t b = px & 0x1F;
      uint8_t r8 = (uint8_t)((r * 255 + 15) / 31);
      uint8_t g8 = (uint8_t)((g * 255 + 31) / 63);
      uint8_t b8 = (uint8_t)((b * 255 + 15) / 31);
      r8 += (uint8_t)((255 - r8) * t);
      g8 += (uint8_t)((255 - g8) * t);
      b8 += (uint8_t)((255 - b8) * t);
      spr.drawPixel(x, y, spr.color565(r8, g8, b8));
    }
  }
}

// Carves the 4 corners of a w x h rectangle at (x,y) within spr back to bg,
// turning a plain filled rectangle into a rounded-corner "pill". Works
// regardless of what colour(s) are under the corners (e.g. a two-tone
// badge), since it only ever touches the outer r x r corner squares.
void carveRoundCorners(TFT_eSprite &spr, int x, int y, int w, int h, int r, uint16_t bg) {
  for (int cy = 0; cy < r; cy++) {
    int dy = r - cy;
    for (int cx = 0; cx < r; cx++) {
      int dx = r - cx;
      if (dx * dx + dy * dy > r * r) {
        spr.drawPixel(x + cx,         y + cy,         bg);
        spr.drawPixel(x + w - 1 - cx, y + cy,         bg);
        spr.drawPixel(x + cx,         y + h - 1 - cy, bg);
        spr.drawPixel(x + w - 1 - cx, y + h - 1 - cy, bg);
      }
    }
  }
}

void drawRainbowGridDigitCell(int col, char ch) {
  int x = colX(col);
  digitSpr.fillSprite(COL_BG);
  digitSpr.setTextColor(COL_DIGIT_PALETTE[col], COL_BG);
  digitSpr.setTextDatum(MC_DATUM);
  digitSpr.drawString(String(ch), CELL_DIGIT_W / 2, CLOCK_H / 2);
  applyDigitGloss(digitSpr, CELL_DIGIT_W, CLOCK_H);
  digitSpr.pushSprite(x, CLOCK_TOP);
}

// ---- Retro flip-clock face ---------------------------------------------
// A white rounded "card" per digit with a dark seam straight across the
// middle (like a real split-flap display) and a soft shadow just below it,
// where the lower flap would sit a little behind the upper one.
const uint16_t COL_FLIP_CARD  = TFT_WHITE;
const uint16_t COL_FLIP_DIGIT = tft.color565(20, 20, 20);
const uint16_t COL_FLIP_HINGE = tft.color565(10, 10, 10);
const int FLIP_MARGIN = 3;
const int FLIP_RADIUS = 6;

void drawRetroFlipDigitCell(int col, char ch) {
  int x = colX(col);
  int cardW = CELL_DIGIT_W - 2 * FLIP_MARGIN;
  int cardH = CLOCK_H - 2 * FLIP_MARGIN;

  digitSpr.fillSprite(COL_BG);
  digitSpr.fillRect(FLIP_MARGIN, FLIP_MARGIN, cardW, cardH, COL_FLIP_CARD);
  carveRoundCorners(digitSpr, FLIP_MARGIN, FLIP_MARGIN, cardW, cardH, FLIP_RADIUS, COL_BG);

  digitSpr.setTextColor(COL_FLIP_DIGIT, COL_FLIP_CARD);
  digitSpr.setTextDatum(MC_DATUM);
  digitSpr.drawString(String(ch), CELL_DIGIT_W / 2, CLOCK_H / 2);

  int midY = CLOCK_H / 2;
  digitSpr.fillRect(FLIP_MARGIN, midY - 1, cardW, 2, COL_FLIP_HINGE);
  const int shadowRows = 5;
  for (int i = 0; i < shadowRows; i++) {
    uint8_t shade = 60 + (195 * i) / shadowRows; // dark, fading down to white
    digitSpr.drawFastHLine(FLIP_MARGIN, midY + 1 + i, cardW, digitSpr.color565(shade, shade, shade));
  }

  digitSpr.pushSprite(x, CLOCK_TOP);
}

void drawDigitCell(int col, char ch) {
  if (currentFace == FACE_RETRO_FLIP) {
    drawRetroFlipDigitCell(col, ch);
  } else {
    drawRainbowGridDigitCell(col, ch);
  }
}

void drawColonCell(int col, bool visible) {
  int x = colX(col);
  colonSpr.fillSprite(COL_BG);
  if (visible) {
    int cx = CELL_COLON_W / 2;
    int cy = CLOCK_H / 2;
    int r = max(3, CELL_COLON_W / 6);
    int gap = CLOCK_H / 6;
    colonSpr.fillSmoothCircle(cx, cy - gap, r, COL_COLON, COL_BG);
    colonSpr.fillSmoothCircle(cx, cy + gap, r, COL_COLON, COL_BG);
  }
  colonSpr.pushSprite(x, CLOCK_TOP);
}

// Draws a single-colour rounded pill for badge b, with 1 or 2 centred text
// parts, and pushes it to the screen.
void drawPillBadge(TFT_eSprite &spr, const Badge &b, const String &part1, uint16_t col1,
                    const String &part2, uint16_t col2) {
  spr.fillSprite(COL_BG);
  int pillH = TOPBAR_H - 2 * BADGE_MARGIN_Y;
  spr.fillRect(0, BADGE_MARGIN_Y, b.w, pillH, b.color);
  carveRoundCorners(spr, 0, BADGE_MARGIN_Y, b.w, pillH, BADGE_RADIUS, COL_BG);

  spr.setFreeFont(&FreeSansBold9pt7b);
  int w1 = part1.length() ? spr.textWidth(part1) : 0;
  int w2 = part2.length() ? spr.textWidth(part2) : 0;
  int startX = (b.w - (w1 + w2)) / 2;
  int midY = TOPBAR_H / 2;
  spr.setTextDatum(ML_DATUM);
  if (w1) {
    spr.setTextColor(col1, b.color);
    spr.drawString(part1, startX, midY);
  }
  if (w2) {
    spr.setTextColor(col2, b.color);
    spr.drawString(part2, startX + w1, midY);
  }
  spr.pushSprite(b.x, 0);
}

// Month/day badge: one rounded pill, split into a red "month" half and a
// white "day" half with a straight seam in the middle - matches the
// reference photo's two-tone date badge.
void drawMonthDayBadge(int mon, int mday) {
  const Badge &b = B_MDAY;
  int pillH = TOPBAR_H - 2 * BADGE_MARGIN_Y;
  int splitX = (b.w * 42) / 100;

  mdaySpr.fillSprite(COL_BG);
  mdaySpr.fillRect(0, BADGE_MARGIN_Y, splitX, pillH, COL_MONTH_BG);
  mdaySpr.fillRect(splitX, BADGE_MARGIN_Y, b.w - splitX, pillH, COL_DAY_BG);
  carveRoundCorners(mdaySpr, 0, BADGE_MARGIN_Y, b.w, pillH, BADGE_RADIUS, COL_BG);

  char monBuf[3], dayBuf[3];
  snprintf(monBuf, sizeof(monBuf), "%02d", mon);
  snprintf(dayBuf, sizeof(dayBuf), "%02d", mday);

  mdaySpr.setFreeFont(&FreeSansBold9pt7b);
  mdaySpr.setTextDatum(MC_DATUM);
  int midY = TOPBAR_H / 2;
  mdaySpr.setTextColor(COL_MONTH_TXT, COL_MONTH_BG);
  mdaySpr.drawString(monBuf, splitX / 2, midY);
  mdaySpr.setTextColor(COL_DAY_TXT, COL_DAY_BG);
  mdaySpr.drawString(dayBuf, splitX + (b.w - splitX) / 2, midY);

  mdaySpr.pushSprite(b.x, 0);
}

void drawWifiBadge(bool connected, int rssi) {
  const Badge &b = B_WIFI;
  uint16_t bg = connected ? b.color : COL_WIFI_BAD;
  uint16_t iconCol = connected ? COL_WIFI_ICON : COL_WIFI_BAD_ICON;
  int pillH = TOPBAR_H - 2 * BADGE_MARGIN_Y;

  wifiSpr.fillSprite(COL_BG);
  wifiSpr.fillRect(0, BADGE_MARGIN_Y, b.w, pillH, bg);
  carveRoundCorners(wifiSpr, 0, BADGE_MARGIN_Y, b.w, pillH, BADGE_RADIUS, COL_BG);

  int bars = 0;
  if (connected) {
    if (rssi > -55) bars = 4;
    else if (rssi > -65) bars = 3;
    else if (rssi > -75) bars = 2;
    else bars = 1;
  }

  int baseX = b.w / 2 - 8;
  int baseY = TOPBAR_H - 8;
  for (int i = 0; i < 4; i++) {
    int barH = 3 + i * 3;
    int bx = baseX + i * 4;
    if (i < bars) {
      wifiSpr.fillRect(bx, baseY - barH, 3, barH, iconCol);
    } else {
      wifiSpr.drawRect(bx, baseY - barH, 3, barH, iconCol);
    }
  }
  wifiSpr.pushSprite(b.x, 0);
}

} // namespace

namespace ClockDisplay {

void begin() {
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  tft.init();
  tft.setRotation(3); // landscape, 320x170. Try 1 if the image is upside down.
  tft.fillScreen(COL_BG);

  digitSpr.setColorDepth(16);
  colonSpr.setColorDepth(16);
  yearSpr.setColorDepth(16);
  mdaySpr.setColorDepth(16);
  weekSpr.setColorDepth(16);
  doySpr.setColorDepth(16);
  wifiSpr.setColorDepth(16);

  digitSpr.createSprite(CELL_DIGIT_W, CLOCK_H);
  colonSpr.createSprite(CELL_COLON_W, CLOCK_H);
  yearSpr.createSprite(B_YEAR.w, TOPBAR_H);
  mdaySpr.createSprite(B_MDAY.w, TOPBAR_H);
  weekSpr.createSprite(B_WEEK.w, TOPBAR_H);
  doySpr.createSprite(B_DOY.w, TOPBAR_H);
  wifiSpr.createSprite(B_WIFI.w, TOPBAR_H);

  // Anti-aliased "beautiful curved" digit font (Fredoka Bold), loaded once
  // and left resident on digitSpr for the life of the program - loadFont()
  // parses metrics into RAM/PSRAM, which is wasted work to redo every
  // second.
  digitSpr.loadFont(FredokaDigits92);

  drawGrid();
  gridDrawn = true;
}

// Cycles to the next clock face (called on a BOOT-button tap) and forces a
// full repaint on the next update() call, so the switch is visible right
// away instead of waiting for a digit to actually change.
void nextFace() {
  currentFace = (currentFace + 1) % FACE_COUNT;
  gridDrawn = false;
}

void showBootMessage(const String &line1, const String &line2) {
  tft.fillScreen(COL_BG);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(TFT_WHITE, COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(line1, SCR_W / 2, SCR_H / 2 - (line2.length() ? 14 : 0));
  if (line2.length()) {
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.drawString(line2, SCR_W / 2, SCR_H / 2 + 16);
  }
  tft.setFreeFont(nullptr);
  gridDrawn = false; // force redraw of the clock grid once we leave this screen
  for (int i = 0; i < CELL_COUNT; i++) lastDigit[i] = 0;
}

void showSetupScreen(const String &apName, const String &apIP) {
  tft.fillScreen(COL_YEAR_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(TFT_WHITE, COL_YEAR_BG);
  tft.drawString("WiFi Setup", SCR_W / 2, 34);

  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.drawString("Connect your phone to:", SCR_W / 2, 68);
  tft.setTextColor(TFT_YELLOW, COL_YEAR_BG);
  tft.drawString(apName, SCR_W / 2, 92);
  tft.setTextColor(TFT_WHITE, COL_YEAR_BG);
  tft.drawString("Then open http://" + apIP, SCR_W / 2, 122);
  tft.drawString("to enter your WiFi + password", SCR_W / 2, 144);

  tft.setFreeFont(nullptr);
  gridDrawn = false;
  for (int i = 0; i < CELL_COUNT; i++) lastDigit[i] = 0;
}

void update(const struct tm &timeinfo, bool timeValid, bool wifiConnected, int rssi) {
  if (!timeValid) return;

  if (!gridDrawn) {
    tft.fillScreen(COL_BG);
    drawGrid();
    gridDrawn = true;
    lastDateStr = "\x01";
    lastMonthDayStr = "\x01";
    lastWeekStr = "\x01";
    lastYday = -999;
    lastWifiConnected = !wifiConnected; // force redraw
    lastWifiBars = -999;
    lastColonVisible = -1;
    for (int i = 0; i < CELL_COUNT; i++) lastDigit[i] = 0;
  }

  // ---- clock digits ----
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d%02d%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  // buf: H H M M S S  -> map into the 8 cells (2 colon cells in between)
  // The colon dots blink once a second (on for even seconds, off for odd).
  int colonVisible = (timeinfo.tm_sec % 2 == 0) ? 1 : 0;
  const char *src = buf;
  int srcIdx = 0;
  for (int col = 0; col < CELL_COUNT; col++) {
    if (isColonCell(col)) {
      if (colonVisible != lastColonVisible) {
        drawColonCell(col, colonVisible);
      }
      continue;
    }
    char ch = src[srcIdx++];
    if (lastDigit[col] != ch) {
      drawDigitCell(col, ch);
      lastDigit[col] = ch;
    }
  }
  lastColonVisible = colonVisible;

  // ---- year badge ----
  char yearBuf[5];
  snprintf(yearBuf, sizeof(yearBuf), "%04d", timeinfo.tm_year + 1900);
  String yearStr(yearBuf);
  if (yearStr != lastDateStr) {
    lastDateStr = yearStr;
    drawPillBadge(yearSpr, B_YEAR, yearStr, COL_YEAR_TXT, "", COL_YEAR_TXT);
  }

  // ---- month/day badge (two-tone: red month, white day) ----
  char mdayBuf[6];
  snprintf(mdayBuf, sizeof(mdayBuf), "%02d-%02d", timeinfo.tm_mon + 1, timeinfo.tm_mday);
  String mdayStr(mdayBuf);
  if (mdayStr != lastMonthDayStr) {
    lastMonthDayStr = mdayStr;
    drawMonthDayBadge(timeinfo.tm_mon + 1, timeinfo.tm_mday);
  }

  // ---- weekday badge ----
  static const char *WD[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  String weekStr = WD[timeinfo.tm_wday];
  if (weekStr != lastWeekStr) {
    lastWeekStr = weekStr;
    drawPillBadge(weekSpr, B_WEEK, weekStr, COL_WEEK_TXT, "", COL_WEEK_TXT);
  }

  // ---- day-of-year badge ----
  if (timeinfo.tm_yday != lastYday) {
    lastYday = timeinfo.tm_yday;
    char doyBuf[10];
    snprintf(doyBuf, sizeof(doyBuf), "DAY %03d", timeinfo.tm_yday + 1);
    drawPillBadge(doySpr, B_DOY, String(doyBuf), COL_BADGE_TXT, "", COL_BADGE_TXT);
  }

  // ---- wifi badge ----
  int bars = -1;
  if (wifiConnected) {
    if (rssi > -55) bars = 4;
    else if (rssi > -65) bars = 3;
    else if (rssi > -75) bars = 2;
    else bars = 1;
  } else {
    bars = 0;
  }
  if (wifiConnected != lastWifiConnected || bars != lastWifiBars) {
    lastWifiConnected = wifiConnected;
    lastWifiBars = bars;
    drawWifiBadge(wifiConnected, rssi);
  }
}

} // namespace ClockDisplay
