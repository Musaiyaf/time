#include "clock_display.h"
#include "config.h"
#include "sd_card.h"
#include <TFT_eSPI.h>
#include "FredokaDigits87.h"
#include "BebasDigits123.h"
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
// Colours are per-face (see BadgeTheme below) so the status bar re-skins
// along with the clock face instead of staying the same rainbow palette
// no matter which face is showing.
const uint16_t COL_WIFI_BAD      = tft.color565(214, 74, 74); // universal
const uint16_t COL_WIFI_BAD_ICON = TFT_WHITE;                 // "no wifi" warning
const uint16_t COL_SETUP_BG      = tft.color565(27, 111, 209); // WiFi setup screen only

struct BadgeTheme {
  uint16_t yearBg, yearTxt;
  uint16_t monthBg, monthTxt;
  uint16_t dayBg, dayTxt;
  uint16_t weekBg, weekTxt;
  uint16_t doyBg, doyTxt;
  uint16_t wifiBg, wifiIcon;
};

// Rainbow grid face: the original colourful badge row.
const BadgeTheme THEME_RAINBOW = {
  tft.color565(27, 111, 209),  tft.color565(15, 18, 26),    // year
  tft.color565(224, 34, 45),   TFT_WHITE,                   // month
  TFT_WHITE,                   tft.color565(20, 20, 20),    // day
  tft.color565(255, 205, 30),  tft.color565(35, 28, 10),    // week
  tft.color565(224, 133, 45),  TFT_WHITE,                   // day-of-year
  tft.color565(196, 238, 242), tft.color565(25, 60, 80),    // wifi
};

// Retro LED face: dark "unlit segment" backgrounds with bright red/amber
// text, matching the 7-segment digits' own colour family.
const BadgeTheme THEME_LED = {
  tft.color565(35, 10, 8),  tft.color565(255, 100, 60),   // year
  tft.color565(45, 12, 8),  tft.color565(255, 70, 45),    // month
  tft.color565(18, 6, 5),   tft.color565(150, 45, 30),    // day
  tft.color565(35, 10, 8),  tft.color565(255, 140, 40),   // week
  tft.color565(35, 10, 8),  tft.color565(255, 70, 45),    // day-of-year
  tft.color565(28, 8, 6),   tft.color565(255, 70, 45),    // wifi
};

// Gold face: near-black badges with warm gold text, matching the metallic
// gold digits' own colour.
const BadgeTheme THEME_GOLD = {
  tft.color565(20, 16, 8),  tft.color565(205, 165, 70),   // year
  tft.color565(24, 18, 8),  tft.color565(205, 165, 70),   // month
  tft.color565(14, 11, 5),  tft.color565(150, 118, 50),   // day
  tft.color565(20, 16, 8),  tft.color565(205, 165, 70),   // week
  tft.color565(20, 16, 8),  tft.color565(205, 165, 70),   // day-of-year
  tft.color565(16, 13, 6),  tft.color565(205, 165, 70),   // wifi
};

// ---- Layout -----------------------------------------------------------
const int SCR_W = TFT_SCREEN_WIDTH;
const int SCR_H = TFT_SCREEN_HEIGHT;

const int TOPBAR_H = 30;

// Pill geometry: each badge sprite is filled solid, then its 4 outer
// corners are carved back to the (black) background to round them off,
// with a few pixels of black gap left between neighbouring badges.
const int BADGE_MARGIN_Y = 3;
const int BADGE_RADIUS   = 6;

// Badge geometry only - colours come from the current BadgeTheme instead
// of being baked in, so the same Badge works for every face.
struct Badge { int x, w; };
const Badge B_YEAR  = {3,   50};
const Badge B_MDAY  = {56,  74};   // month+day, two-tone
const Badge B_WEEK  = {133, 76};
const Badge B_DOY   = {212, 76};
const Badge B_WIFI  = {291, 26};

const int CLOCK_TOP = TOPBAR_H;
const int CLOCK_H   = SCR_H - TOPBAR_H;

// The clock face is HH:MM:SS -> 6 digit cells + 2 (narrower) colon cells;
// colon cells hold two small blinking dots, kept narrow so HH/MM/SS read as
// tight groups rather than 6 evenly-spaced digits. The two widths add up to
// exactly SCR_W (320): 51*6 + 7*2 = 320.
//
// IMPORTANT: every glyph in the smooth font must be strictly NARROWER than
// CELL_DIGIT_W. TFT_eSPI does not clip an oversized smooth-font glyph, it
// skips drawing it entirely - so a font whose widest glyphs exceed this
// width makes those particular digits (e.g. 0/2/3/4) invisible while the
// narrower ones still render. FredokaDigits87's widest glyph is 49px, so
// it fits any CELL_DIGIT_W >= 50; regenerate it if you shrink this further.
const int CELL_COUNT = 8;
const int CELL_DIGIT_W = 51;
const int CELL_COLON_W = 7;
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
// The status badge row re-skins along with the big HH:MM:SS area (and
// whether it gets the dashed grid lines) - see BadgeTheme above.
enum ClockFaceId {
  FACE_RAINBOW_GRID = 0,
  FACE_SEVEN_SEG = 1,
  FACE_GOLD = 2,
  FACE_CUSTOM = 3,
  FACE_COUNT = 4
};
int currentFace = FACE_RAINBOW_GRID;

// Rich gold, used for both the Gold face's digits and its colon dots.
const uint16_t COL_GOLD = tft.color565(205, 165, 70);

// ---- Custom face: a user-supplied background image + colours, loaded
// from an optional SD card (see sd_card.h). Entirely optional - with no
// card, or no /faces/custom/ on it, this face just falls back to plain
// white-on-black digits, the same as if you'd never touched it.
struct CustomFaceConfig {
  uint16_t digitColor = TFT_WHITE;
  uint16_t accentColor = TFT_CYAN;
  bool hasBackground = false;
  // Which of the two fonts already built into this firmware to use for
  // the digits - 0 = Fredoka (rounded), 1 = Bebas (tall/condensed,
  // default, matches this face's original look). Adding a genuinely new
  // third font means converting and compiling a new glyph table, which
  // isn't something this config file can do - see the "font" key in
  // tools/make_custom_face.py|.html for the two that exist.
  int fontId = 1;
};
CustomFaceConfig customCfg;
// SCR_W * CLOCK_H raw RGB565 pixels, allocated once (lazily, in PSRAM) the
// first time the Custom face is actually opened. Kept for the rest of the
// session, same "never freed/recreated" policy as the sprites above.
uint16_t *customBgBuf = nullptr;

uint16_t parseHexColor(const String &s, uint16_t fallback) {
  if (s.length() != 7 || s[0] != '#') return fallback;
  long v = strtol(s.c_str() + 1, nullptr, 16);
  return tft.color565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

// Parses a simple "key=value" text config (one per line, '#' comments,
// blank lines ignored) - deliberately not JSON, to avoid pulling in a
// parsing library for two colour fields.
void loadCustomFaceConfig() {
  customCfg = CustomFaceConfig();
  String text = SdCard::readTextFile("/faces/custom/face.cfg");
  int start = 0;
  while (start < (int)text.length()) {
    int nl = text.indexOf('\n', start);
    if (nl < 0) nl = text.length();
    String line = text.substring(start, nl);
    line.trim();
    start = nl + 1;
    if (line.length() == 0 || line[0] == '#') continue;
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    key.trim();
    val.trim();
    if (key == "digit_color") customCfg.digitColor = parseHexColor(val, customCfg.digitColor);
    else if (key == "accent_color") customCfg.accentColor = parseHexColor(val, customCfg.accentColor);
    else if (key == "font") customCfg.fontId = (val == "fredoka") ? 0 : 1;
  }
}

// Loads the config + background the first time (and only the first time)
// the Custom face is actually opened this session - see nextFace()/
// prevFace(). Re-reading on every visit isn't worth the SD traffic for a
// file that's expected to change rarely, if ever, while running.
void ensureCustomFaceLoaded() {
  if (currentFace != FACE_CUSTOM) return;
  static bool attempted = false;
  if (attempted) return;
  attempted = true;

  loadCustomFaceConfig();
  if (!customBgBuf) {
    customBgBuf = (uint16_t *)ps_malloc((size_t)SCR_W * CLOCK_H * sizeof(uint16_t));
  }
  if (customBgBuf) {
    customCfg.hasBackground = SdCard::readImage("/faces/custom/bg.bin", customBgBuf, SCR_W, CLOCK_H);
  }
}

const BadgeTheme &badgeTheme() {
  if (currentFace == FACE_SEVEN_SEG) return THEME_LED;
  if (currentFace == FACE_GOLD) return THEME_GOLD;
  if (currentFace == FACE_CUSTOM) {
    static BadgeTheme customTheme;
    uint16_t bg = tft.color565(10, 10, 14);
    customTheme = {bg, customCfg.accentColor, bg, customCfg.accentColor, bg, customCfg.accentColor,
                   bg, customCfg.accentColor, bg, customCfg.accentColor, bg, customCfg.accentColor};
    return customTheme;
  }
  return THEME_RAINBOW;
}

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

uint16_t blend565(uint16_t c1, uint16_t c2, float t) {
  int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  int r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
  int r = r1 + (int)((r2 - r1) * t + 0.5f);
  int g = g1 + (int)((g2 - g1) * t + 0.5f);
  int b = b1 + (int)((b2 - b1) * t + 0.5f);
  return (uint16_t)(((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F));
}

// Carves the 4 corners of a w x h rectangle at (x,y) within spr back to bg,
// turning a plain filled rectangle into a rounded-corner "pill". Works
// regardless of what colour(s) are under the corners (e.g. a two-tone
// badge), since it only ever touches the outer r x r corner squares.
//
// Each corner pixel's coverage is estimated by supersampling a 4x4 grid of
// points inside it and counting how many fall outside the radius, giving
// 17 possible blend levels towards bg rather than a simple 1px linear
// falloff - at the small radii used here (5-6px), a single-sample edge
// test or a crude linear blend still reads as a visible staircase, so this
// needs the extra samples to look genuinely smooth.
const int CORNER_SS = 4;

void carveRoundCorners(TFT_eSprite &spr, int x, int y, int w, int h, int r, uint16_t bg) {
  for (int cy = 0; cy <= r; cy++) {
    for (int cx = 0; cx <= r; cx++) {
      int outside = 0;
      for (int sy = 0; sy < CORNER_SS; sy++) {
        float py = cy + (sy + 0.5f) / CORNER_SS;
        float dy = r - py;
        for (int sx = 0; sx < CORNER_SS; sx++) {
          float px = cx + (sx + 0.5f) / CORNER_SS;
          float dx = r - px;
          if (dx * dx + dy * dy > (float)r * r) outside++;
        }
      }
      if (outside == 0) continue; // fully inside the curve - untouched
      float alpha = (float)outside / (CORNER_SS * CORNER_SS);
      int xs[2] = { x + cx, x + w - 1 - cx };
      int ys[2] = { y + cy, y + h - 1 - cy };
      for (int xi = 0; xi < 2; xi++) {
        for (int yi = 0; yi < 2; yi++) {
          uint16_t cur = spr.readPixel(xs[xi], ys[yi]);
          spr.drawPixel(xs[xi], ys[yi], blend565(cur, bg, alpha));
        }
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

// ---- Retro LED (7-segment) face -----------------------------------------
// Classic digital-alarm-clock look: bright red segments on black, plus a
// faint "ghost" of the unlit segments (like a real LED/LCD 7-segment
// display, where you can always see the whole figure-8 outline).
const uint16_t COL_LED_ON  = tft.color565(255, 40, 40);  // bright red, lit
const uint16_t COL_LED_OFF = tft.color565(40, 10, 10);   // dim red, unlit
const int LED_MARGIN = 5;  // gap between the digit box and the cell edge
const int LED_THICK  = 8;  // segment stroke thickness
const int LED_RADIUS = 2;  // slight rounding on each segment's corners

// Which of the 7 segments (a=top, b=top-right, c=bottom-right, d=bottom,
// e=bottom-left, f=top-left, g=middle) are lit for each digit 0-9.
const bool SEVEN_SEG[10][7] = {
  {1, 1, 1, 1, 1, 1, 0}, // 0
  {0, 1, 1, 0, 0, 0, 0}, // 1
  {1, 1, 0, 1, 1, 0, 1}, // 2
  {1, 1, 1, 1, 0, 0, 1}, // 3
  {0, 1, 1, 0, 0, 1, 1}, // 4
  {1, 0, 1, 1, 0, 1, 1}, // 5
  {1, 0, 1, 1, 1, 1, 1}, // 6
  {1, 1, 1, 0, 0, 0, 0}, // 7
  {1, 1, 1, 1, 1, 1, 1}, // 8
  {1, 1, 1, 1, 0, 1, 1}, // 9
};

void drawLedSegment(int x, int y, int w, int h, uint16_t color) {
  digitSpr.fillRect(x, y, w, h, color);
  int r = min(LED_RADIUS, min(w, h) / 2);
  if (r > 0) carveRoundCorners(digitSpr, x, y, w, h, r, COL_BG);
}

void drawSevenSegDigitCell(int col, char ch) {
  int x = colX(col);
  digitSpr.fillSprite(COL_BG);

  int W = CELL_DIGIT_W - 2 * LED_MARGIN;
  int H = CLOCK_H - 2 * LED_MARGIN;
  int T = LED_THICK;
  int x0 = LED_MARGIN, y0 = LED_MARGIN;
  int gapTop = H / 2 - T / 2; // where the upper vertical segments end
  int gapBot = H / 2 + T / 2; // where the lower vertical segments start
  int vH = gapTop - T;        // height of each vertical segment

  // Segment boxes, indexed a,b,c,d,e,f,g - all relative to (x0, y0).
  int sx[7] = { T,     W - T, W - T, T,     0,     0,     T     };
  int sy[7] = { 0,     T,     gapBot, H - T, gapBot, T,    gapTop };
  int sw[7] = { W - 2*T, T,   T,     W - 2*T, T,    T,    W - 2*T };
  int sh[7] = { T,     vH,    vH,    T,     vH,    vH,    T     };

  int digit = ch - '0';
  for (int s = 0; s < 7; s++) {
    bool on = (digit >= 0 && digit <= 9) && SEVEN_SEG[digit][s];
    drawLedSegment(x0 + sx[s], y0 + sy[s], sw[s], sh[s], on ? COL_LED_ON : COL_LED_OFF);
  }

  digitSpr.pushSprite(x, CLOCK_TOP);
}

// ---- Gold face -----------------------------------------------------------
// Bold gold digits in BebasDigits123, with the same top-lit gloss the
// rainbow face uses (applyDigitGloss() above) - brightening a solid gold
// fill towards white at the top reads as a bright metallic highlight
// glinting off the top of each numeral, the same trick a lot of real
// gold/chrome text effects use.
void drawGoldDigitCell(int col, char ch) {
  int x = colX(col);
  digitSpr.fillSprite(COL_BG);
  digitSpr.setTextColor(COL_GOLD, COL_BG);
  digitSpr.setTextDatum(MC_DATUM);
  digitSpr.drawString(String(ch), CELL_DIGIT_W / 2, CLOCK_H / 2);
  applyDigitGloss(digitSpr, CELL_DIGIT_W, CLOCK_H);
  digitSpr.pushSprite(x, CLOCK_TOP);
}

// digitSpr holds one smooth font at a time (Fredoka for the rainbow face,
// Bebas for the gold face - the LED face doesn't use a font at all).
// Custom face picks between the two via customCfg.fontId (from face.cfg -
// see ensureCustomFaceLoaded(), which must run before this so the choice
// is already loaded by the time this checks it). Reloading a font takes a
// moment to parse, so this only runs when the target face actually needs
// a different font than what's currently loaded, rather than on every
// digit redraw.
void ensureDigitFont() {
  static int loadedFont = -1; // -1 = none yet, 0 = Fredoka, 1 = Bebas
  int needed = currentFace == FACE_CUSTOM ? customCfg.fontId
              : currentFace == FACE_GOLD  ? 1
                                           : 0;
  if (needed == loadedFont) return;
  digitSpr.unloadFont();
  if (needed == 1) digitSpr.loadFont(BebasDigits123);
  else digitSpr.loadFont(FredokaDigits87);
  loadedFont = needed;
}

// Copies a CELL_DIGIT_W (or CELL_COLON_W)-wide, CLOCK_H-tall slice of the
// cached Custom Face background at column x into spr, pixel by pixel.
// Deliberately uses drawPixel() rather than pushImage(): drawPixel() is
// already proven correct elsewhere in this file (applyDigitGloss(),
// carveRoundCorners() both read/write pixels this way on these same
// sprites), whereas pushImage() turned out to still produce scrambled
// "TV static" colours here even after correcting its swap-bytes state -
// something about its handling of a sub-rectangle pulled out of a larger
// source buffer, on a sprite that also has a smooth font loaded, wasn't
// behaving as documented. drawPixel() sidesteps that class of bug
// entirely by writing each already-known-good RGB565 value directly.
void pushCustomBgSlice(TFT_eSprite &spr, int x, int w) {
  for (int row = 0; row < CLOCK_H; row++) {
    const uint16_t *src = customBgBuf + row * SCR_W + x;
    for (int col = 0; col < w; col++) {
      spr.drawPixel(col, row, src[col]);
    }
  }
}

// Custom face: the cached background (if any) shows through everywhere
// except the glyph itself, via TFT_eSPI's transparent text mode
// (setTextColor with a single colour argument only paints foreground
// pixels, unlike the two-argument opaque form used by the other faces).
void drawCustomDigitCell(int col, char ch) {
  int x = colX(col);
  if (customBgBuf && customCfg.hasBackground) {
    pushCustomBgSlice(digitSpr, x, CELL_DIGIT_W);
  } else {
    digitSpr.fillSprite(COL_BG);
  }
  digitSpr.setTextColor(customCfg.digitColor);
  digitSpr.setTextDatum(MC_DATUM);
  digitSpr.drawString(String(ch), CELL_DIGIT_W / 2, CLOCK_H / 2);
  digitSpr.pushSprite(x, CLOCK_TOP);
}

void drawDigitCell(int col, char ch) {
  if (currentFace == FACE_SEVEN_SEG) {
    drawSevenSegDigitCell(col, ch);
  } else if (currentFace == FACE_GOLD) {
    drawGoldDigitCell(col, ch);
  } else if (currentFace == FACE_CUSTOM) {
    drawCustomDigitCell(col, ch);
  } else {
    drawRainbowGridDigitCell(col, ch);
  }
}

void drawColonCell(int col, bool visible) {
  int x = colX(col);
  bool customBg = currentFace == FACE_CUSTOM && customBgBuf && customCfg.hasBackground;
  if (customBg) {
    pushCustomBgSlice(colonSpr, x, CELL_COLON_W);
  } else {
    colonSpr.fillSprite(COL_BG);
  }
  if (visible) {
    int cx = CELL_COLON_W / 2;
    int cy = CLOCK_H / 2;
    int r = max(3, CELL_COLON_W / 6);
    int gap = CLOCK_H / 6;
    uint16_t dotColor = (currentFace == FACE_CUSTOM) ? customCfg.digitColor
                       : (currentFace == FACE_GOLD)   ? COL_GOLD
                                                       : COL_COLON;
    colonSpr.fillSmoothCircle(cx, cy - gap, r, dotColor, COL_BG);
    colonSpr.fillSmoothCircle(cx, cy + gap, r, dotColor, COL_BG);
  }
  colonSpr.pushSprite(x, CLOCK_TOP);
}

// Draws a single-colour rounded pill for badge b, with 1 or 2 centred text
// parts, and pushes it to the screen.
void drawPillBadge(TFT_eSprite &spr, const Badge &b, uint16_t bg, const String &part1, uint16_t col1,
                    const String &part2, uint16_t col2) {
  spr.fillSprite(COL_BG);
  int pillH = TOPBAR_H - 2 * BADGE_MARGIN_Y;
  spr.fillRect(0, BADGE_MARGIN_Y, b.w, pillH, bg);

  spr.setFreeFont(&FreeSansBold9pt7b);
  int w1 = part1.length() ? spr.textWidth(part1) : 0;
  int w2 = part2.length() ? spr.textWidth(part2) : 0;
  int startX = (b.w - (w1 + w2)) / 2;
  int midY = TOPBAR_H / 2;
  spr.setTextDatum(ML_DATUM);
  if (w1) {
    spr.setTextColor(col1, bg);
    spr.drawString(part1, startX, midY);
  }
  if (w2) {
    spr.setTextColor(col2, bg);
    spr.drawString(part2, startX + w1, midY);
  }

  // Carve LAST, after the text. drawString() is called with an opaque
  // background colour, so TFT_eSPI paints a filled box behind every glyph -
  // carving first meant that box repainted the corners solid again, most
  // visibly on the widest text ("DAY 232" nearly spans its pill).
  carveRoundCorners(spr, 0, BADGE_MARGIN_Y, b.w, pillH, BADGE_RADIUS, COL_BG);
  spr.pushSprite(b.x, 0);
}

// Draws each character of s individually with gap extra pixels between
// them, as a group centred on (cx, cy). Plain drawString() sets consecutive
// characters edge-to-edge; this loosens that up a bit for a two-character
// badge value like "08" where the digits otherwise look glued together.
void drawSpacedDigits(TFT_eSprite &spr, const String &s, int cx, int cy, int gap) {
  int n = s.length();
  int widths[4];
  int totalW = 0;
  for (int i = 0; i < n && i < 4; i++) {
    widths[i] = spr.textWidth(String(s[i]));
    totalW += widths[i];
  }
  totalW += gap * (n - 1);
  spr.setTextDatum(ML_DATUM);
  int x = cx - totalW / 2;
  for (int i = 0; i < n && i < 4; i++) {
    spr.drawString(String(s[i]), x, cy);
    x += widths[i] + gap;
  }
}

// Month/day badge: one rounded pill, split into a red "month" half and a
// white "day" half with a straight seam in the middle - matches the
// reference photo's two-tone date badge.
void drawMonthDayBadge(int mon, int mday) {
  const Badge &b = B_MDAY;
  const BadgeTheme &th = badgeTheme();
  int pillH = TOPBAR_H - 2 * BADGE_MARGIN_Y;
  int splitX = (b.w * 42) / 100;

  mdaySpr.fillSprite(COL_BG);
  mdaySpr.fillRect(0, BADGE_MARGIN_Y, splitX, pillH, th.monthBg);
  mdaySpr.fillRect(splitX, BADGE_MARGIN_Y, b.w - splitX, pillH, th.dayBg);

  char monBuf[3], dayBuf[3];
  snprintf(monBuf, sizeof(monBuf), "%02d", mon);
  snprintf(dayBuf, sizeof(dayBuf), "%02d", mday);

  mdaySpr.setFreeFont(&FreeSansBold9pt7b);
  int midY = TOPBAR_H / 2;
  mdaySpr.setTextColor(th.monthTxt, th.monthBg);
  drawSpacedDigits(mdaySpr, monBuf, splitX / 2, midY, 2);
  mdaySpr.setTextDatum(MC_DATUM);
  mdaySpr.setTextColor(th.dayTxt, th.dayBg);
  mdaySpr.drawString(dayBuf, splitX + (b.w - splitX) / 2, midY);

  // Carve last - see the note in drawPillBadge().
  carveRoundCorners(mdaySpr, 0, BADGE_MARGIN_Y, b.w, pillH, BADGE_RADIUS, COL_BG);
  mdaySpr.pushSprite(b.x, 0);
}

void drawWifiBadge(bool connected, int rssi) {
  const Badge &b = B_WIFI;
  const BadgeTheme &th = badgeTheme();
  uint16_t bg = connected ? th.wifiBg : COL_WIFI_BAD;
  uint16_t iconCol = connected ? th.wifiIcon : COL_WIFI_BAD_ICON;
  int pillH = TOPBAR_H - 2 * BADGE_MARGIN_Y;

  wifiSpr.fillSprite(COL_BG);
  wifiSpr.fillRect(0, BADGE_MARGIN_Y, b.w, pillH, bg);

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

  // Carve last - see the note in drawPillBadge(). The leftmost signal bar
  // reaches into the bottom-left corner zone, so this matters here too.
  carveRoundCorners(wifiSpr, 0, BADGE_MARGIN_Y, b.w, pillH, BADGE_RADIUS, COL_BG);
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

  ensureDigitFont(); // loads FredokaDigits87 for the default rainbow face

  drawGrid();
  gridDrawn = true;
}

// Cycles to the next/previous clock face and forces a full repaint on the
// next update() call, so the switch is visible right away instead of
// waiting for a digit to actually change.
void nextFace() {
  currentFace = (currentFace + 1) % FACE_COUNT;
  ensureCustomFaceLoaded(); // must run before ensureDigitFont() - it's what sets customCfg.fontId
  ensureDigitFont();
  gridDrawn = false;
}

void prevFace() {
  currentFace = (currentFace + FACE_COUNT - 1) % FACE_COUNT;
  ensureCustomFaceLoaded(); // must run before ensureDigitFont() - it's what sets customCfg.fontId
  ensureDigitFont();
  gridDrawn = false;
}

void forceFullRedraw() {
  gridDrawn = false;
}

TFT_eSPI &rawDisplay() {
  return tft;
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
  tft.fillScreen(COL_SETUP_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(TFT_WHITE, COL_SETUP_BG);
  tft.drawString("WiFi Setup", SCR_W / 2, 34);

  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.drawString("Connect your phone to:", SCR_W / 2, 68);
  tft.setTextColor(TFT_YELLOW, COL_SETUP_BG);
  tft.drawString(apName, SCR_W / 2, 92);
  tft.setTextColor(TFT_WHITE, COL_SETUP_BG);
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
    const BadgeTheme &th = badgeTheme();
    drawPillBadge(yearSpr, B_YEAR, th.yearBg, yearStr, th.yearTxt, "", th.yearTxt);
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
    const BadgeTheme &th = badgeTheme();
    drawPillBadge(weekSpr, B_WEEK, th.weekBg, weekStr, th.weekTxt, "", th.weekTxt);
  }

  // ---- day-of-year badge ----
  if (timeinfo.tm_yday != lastYday) {
    lastYday = timeinfo.tm_yday;
    char doyBuf[10];
    snprintf(doyBuf, sizeof(doyBuf), "DAY %03d", timeinfo.tm_yday + 1);
    const BadgeTheme &th = badgeTheme();
    drawPillBadge(doySpr, B_DOY, th.doyBg, String(doyBuf), th.doyTxt, "", th.doyTxt);
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
