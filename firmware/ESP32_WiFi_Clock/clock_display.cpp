#include "clock_display.h"
#include "config.h"
#include "sd_card.h"
#include "video_player.h"
#include "PhotoDigits.h"
#include "BotanicalDigits.h"
#include "SilverDigits.h"
#include <TFT_eSPI.h>
#include "FredokaDigits87.h"
// TFT_eSPI.h (with LOAD_GFXFF enabled) already pulls in every Adafruit GFX
// free font, including this one, via its own Fonts/GFXFF/gfxfont.h. That
// font header has no include guard, so including it again here would
// cause a duplicate-definition build error - just use the font directly.

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
TFT_eSprite photoDigitSpr(&tft); // Photo face only - see drawPhotoRow()

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

// Photo face: plain white badge boxes, matching the white background its
// mixed-style digits need (several of them are themselves black/dark-
// brown/dark-navy - see that face's own comment - so it isn't on the
// usual black backdrop the other faces share).
const BadgeTheme THEME_PHOTO = {
  TFT_WHITE, TFT_BLACK,   // year
  TFT_WHITE, TFT_BLACK,   // month
  TFT_WHITE, TFT_BLACK,   // day
  TFT_WHITE, TFT_BLACK,   // week
  TFT_WHITE, TFT_BLACK,   // day-of-year
  TFT_WHITE, TFT_BLACK,   // wifi
};

// Botanical face: the deep vine-green sampled from its digits' own
// illuminated-manuscript artwork, used as both the digit-area background
// and the badge fill - the same "background colour matches the badges"
// pattern Photo's white and the rainbow grid's per-badge colours already
// use, just with one shared colour instead of many. Cream text, sampled
// from the source art's own parchment-page background, reads clearly on
// the green and echoes the manuscript-page look.
const uint16_t COL_BOTANICAL_BG   = tft.color565(39, 93, 77);
const uint16_t COL_BOTANICAL_TEXT = tft.color565(220, 210, 185);
const BadgeTheme THEME_BOTANICAL = {
  COL_BOTANICAL_BG, COL_BOTANICAL_TEXT,   // year
  COL_BOTANICAL_BG, COL_BOTANICAL_TEXT,   // month
  COL_BOTANICAL_BG, COL_BOTANICAL_TEXT,   // day
  COL_BOTANICAL_BG, COL_BOTANICAL_TEXT,   // week
  COL_BOTANICAL_BG, COL_BOTANICAL_TEXT,   // day-of-year
  COL_BOTANICAL_BG, COL_BOTANICAL_TEXT,   // wifi
};

// Silver face: plain black behind the chrome digits (their own crop keeps
// a slice of that same black background baked in - see SilverDigits.h -
// so the digit row's edges disappear into it with no visible seam). The
// badges get a dark gunmetal fill rather than pure black, deliberately
// non-zero: applyDigitGloss() below treats colour 0 as "background, leave
// untouched", so a literally black pill would make the shine effect this
// face was built for silently do nothing.
const uint16_t COL_SILVER_BADGE = tft.color565(58, 60, 66);
const uint16_t COL_SILVER_TEXT  = tft.color565(232, 234, 238);
const BadgeTheme THEME_SILVER = {
  COL_SILVER_BADGE, COL_SILVER_TEXT,   // year
  COL_SILVER_BADGE, COL_SILVER_TEXT,   // month
  COL_SILVER_BADGE, COL_SILVER_TEXT,   // day
  COL_SILVER_BADGE, COL_SILVER_TEXT,   // week
  COL_SILVER_BADGE, COL_SILVER_TEXT,   // day-of-year
  COL_SILVER_BADGE, COL_SILVER_TEXT,   // wifi
};

// Flip Clock face: split-flap "departure board" cards - dark charcoal
// cards on a near-black page, bright white digits, matching a real split-
// flap board (and this codebase's own repeated preference for a dark
// backdrop with bright numerals - Silver, Rainbow, LED all do the same).
// Badges match the page colour, same "background matches the badges"
// pattern Botanical's green and Photo's white already use.
const uint16_t COL_FLIP_BG     = tft.color565(15, 15, 17);   // page behind the cards
const uint16_t COL_FLIP_CARD   = tft.color565(35, 36, 40);   // card face
const uint16_t COL_FLIP_TEXT   = TFT_WHITE;                  // digit ink, at rest
const uint16_t COL_FLIP_SHADOW = tft.color565(90, 92, 98);   // flap tint near edge-on
const uint16_t COL_FLIP_HINGE  = tft.color565(120, 123, 130); // seam highlight line
const uint16_t COL_FLIP_PIN    = tft.color565(150, 153, 160); // hinge pin dots
const uint16_t COL_FLIP_BADGE_TXT = tft.color565(225, 227, 232);
const BadgeTheme THEME_FLIP = {
  COL_FLIP_BG, COL_FLIP_BADGE_TXT,   // year
  COL_FLIP_BG, COL_FLIP_BADGE_TXT,   // month
  COL_FLIP_BG, COL_FLIP_BADGE_TXT,   // day
  COL_FLIP_BG, COL_FLIP_BADGE_TXT,   // week
  COL_FLIP_BG, COL_FLIP_BADGE_TXT,   // day-of-year
  COL_FLIP_BG, COL_FLIP_BADGE_TXT,   // wifi
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
  FACE_VIDEO = 2,
  FACE_PHOTO = 3,
  FACE_BOTANICAL = 4,
  FACE_SILVER = 5,
  FACE_FLIP = 6,
  FACE_COUNT = 7
};
int currentFace = FACE_RAINBOW_GRID;

const BadgeTheme &badgeTheme() {
  if (currentFace == FACE_SEVEN_SEG) return THEME_LED;
  if (currentFace == FACE_PHOTO) return THEME_PHOTO;
  if (currentFace == FACE_BOTANICAL) return THEME_BOTANICAL;
  if (currentFace == FACE_SILVER) return THEME_SILVER;
  if (currentFace == FACE_FLIP) return THEME_FLIP;
  return THEME_RAINBOW;
}

// Silver Face's own gloss pass over the status bar badges - see
// applyDigitGloss()'s definition further down for the actual pixel math
// (shared with the rainbow grid face's digit cells). Only this face wants
// its badges to shine; every other theme keeps its flat pill fill.
bool badgeGlossActive() { return currentFace == FACE_SILVER; }

// ---- state cache, so we only repaint what changed --------------------
char lastDigit[CELL_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
bool gridDrawn = false;

// Digit transition animations - Rainbow Grid (rolling) and Flip Clock
// (split-flap), each with their own timing but sharing this one per-cell
// state and the same start/finish bookkeeping in update() below (see the
// FACE_RAINBOW_GRID/FACE_FLIP branch there). Every other face still does
// the old instant swap.
struct DigitAnim {
  char fromCh = 0;
  char toCh = 0;
  unsigned long startMs = 0;
  bool active = false;
};
DigitAnim digitAnims[CELL_COUNT];
// Rainbow Grid: like a train of digits on a vertical rail track - the old
// digit slides up and off the top of the cell while the new one rises up
// from below to take its place. Modelled as a single strip holding both
// digits (old at the cell's normal centre, new one full cell-height below
// it) that slides upward by `progress` of the cell height - see
// drawRainbowGridDigitCellAnimated().
const unsigned long DIGIT_ANIM_MS = 220;
// Flip Clock: a real split-flap card - see drawFlipDigitCellAnimated().
const unsigned long FLIP_ANIM_MS = 380;
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

// One frame of the rolling transition, `progress` from 0 (old digit still
// dead centre, new digit a full cell-height below it, off screen) to 1
// (old digit a full cell-height above centre, off screen; new digit now
// dead centre - the same position drawRainbowGridDigitCell() would draw
// it statically). Both digits are drawn into the one CELL_DIGIT_W x
// CLOCK_H sprite at their current offsets; TFT_eSprite clips anything
// outside those bounds for free, which is exactly what makes each digit
// look like it's sliding through a fixed window rather than overflowing
// into the cells above/below.
void drawRainbowGridDigitCellAnimated(int col, char fromCh, char toCh, float progress) {
  int x = colX(col);
  int offset = (int)roundf(progress * CLOCK_H);
  digitSpr.fillSprite(COL_BG);
  digitSpr.setTextColor(COL_DIGIT_PALETTE[col], COL_BG);
  digitSpr.setTextDatum(MC_DATUM);
  digitSpr.drawString(String(fromCh), CELL_DIGIT_W / 2, CLOCK_H / 2 - offset);
  digitSpr.drawString(String(toCh), CELL_DIGIT_W / 2, CLOCK_H / 2 - offset + CLOCK_H);
  applyDigitGloss(digitSpr, CELL_DIGIT_W, CLOCK_H);
  digitSpr.pushSprite(x, CLOCK_TOP);
}

// ---- Flip Clock face -----------------------------------------------------
// A split-flap "departure board" card per digit: a dark card face split in
// two by a hinge line, on a near-black page. There's no true 3D rotation
// on a 2D panel, so the flip is approximated the way most software
// recreations do it - a vertical crop rather than a true perspective
// squish - but anchored at the hinge so it still reads as a card folding
// there rather than a curtain closing:
//   - the TOP half's background is always the settling-in NEW digit
//     (like the real board's fixed upper leaf, already updated);
//   - the BOTTOM half's background is always the outgoing OLD digit
//     (not yet replaced);
//   - a single flap animates in two phases, hinged at the seam the whole
//     time: phase 1 (0-50%) collapses the OLD top half down into the
//     hinge, uncovering the new top background as it shrinks; phase 2
//     (50-100%) grows a NEW bottom half back out of the hinge, covering
//     the old bottom background as it expands. The flap's own colour
//     tints from white towards COL_FLIP_SHADOW as it nears the hinge -
//     standing in for the edge-on, side-lit look a real flap gets
//     mid-rotation, since a flat crop with no shading at all read as
//     too flat/mechanical next to a real split-flap board's photos.
// Horizontal clipping is left at the full cell width (proven safe for
// this font - see the CELL_DIGIT_W comment above) even though the card
// itself is drawn narrower, so the widest glyphs never get clipped.
const int FLIP_MARGIN_X = 2; // gap between adjacent digit cards
const int FLIP_MARGIN_Y = 3; // gap above/below each card within its cell
const int FLIP_PIN_R = 1;    // hinge pin dot radius

void drawFlipHingePins(int cardX, int cardW, int hingeY) {
  digitSpr.fillCircle(cardX + 1, hingeY, FLIP_PIN_R, COL_FLIP_PIN);
  digitSpr.fillCircle(cardX + cardW - 2, hingeY, FLIP_PIN_R, COL_FLIP_PIN);
}

void drawFlipDigitCell(int col, char ch) {
  int x = colX(col);
  int cardX = FLIP_MARGIN_X, cardY = FLIP_MARGIN_Y;
  int cardW = CELL_DIGIT_W - 2 * FLIP_MARGIN_X;
  int cardH = CLOCK_H - 2 * FLIP_MARGIN_Y;
  int hingeY = cardY + cardH / 2;

  digitSpr.fillSprite(COL_FLIP_BG);
  digitSpr.fillRoundRect(cardX, cardY, cardW, cardH, 4, COL_FLIP_CARD);
  digitSpr.setTextDatum(MC_DATUM);
  digitSpr.setTextColor(COL_FLIP_TEXT, COL_FLIP_CARD);
  digitSpr.drawString(String(ch), CELL_DIGIT_W / 2, hingeY);
  digitSpr.fillRect(cardX, hingeY - 1, cardW, 2, COL_FLIP_HINGE);
  drawFlipHingePins(cardX, cardW, hingeY);
  digitSpr.pushSprite(x, CLOCK_TOP);
}

void drawFlipDigitCellAnimated(int col, char fromCh, char toCh, float progress) {
  int x = colX(col);
  int cardX = FLIP_MARGIN_X, cardY = FLIP_MARGIN_Y;
  int cardW = CELL_DIGIT_W - 2 * FLIP_MARGIN_X;
  int cardH = CLOCK_H - 2 * FLIP_MARGIN_Y;
  int halfH = cardH / 2;
  int halfH2 = cardH - halfH;
  int hingeY = cardY + halfH;
  int textCy = cardY + cardH / 2; // true full-card centre every glyph is drawn around

  digitSpr.fillSprite(COL_FLIP_BG);
  digitSpr.fillRoundRect(cardX, cardY, cardW, cardH, 4, COL_FLIP_CARD);
  digitSpr.setTextDatum(MC_DATUM);
  digitSpr.setTextColor(COL_FLIP_TEXT, COL_FLIP_CARD);

  // Resting backgrounds: top has already settled on the new digit, bottom
  // hasn't changed yet - only the flap in between is still moving.
  digitSpr.setViewport(0, cardY, CELL_DIGIT_W, halfH);
  digitSpr.drawString(String(toCh), CELL_DIGIT_W / 2, textCy);
  digitSpr.resetViewport();

  digitSpr.setViewport(0, hingeY, CELL_DIGIT_W, halfH2);
  digitSpr.drawString(String(fromCh), CELL_DIGIT_W / 2, textCy);
  digitSpr.resetViewport();

  bool phase1 = progress < 0.5f;
  float p = phase1 ? (progress / 0.5f) : ((progress - 0.5f) / 0.5f);
  if (phase1) {
    int flapH = (int)roundf(halfH * (1.0f - p));
    if (flapH > 0) {
      // Shrinking towards the hinge - the smaller it gets, the more it
      // reads as edge-on, so tint it further towards shadow as flapH
      // drops (p towards 1, i.e. the same fraction the height shrank by).
      digitSpr.setTextColor(blend565(COL_FLIP_TEXT, COL_FLIP_SHADOW, p), COL_FLIP_CARD);
      digitSpr.setViewport(0, hingeY - flapH, CELL_DIGIT_W, flapH);
      digitSpr.drawString(String(fromCh), CELL_DIGIT_W / 2, textCy);
      digitSpr.resetViewport();
    }
  } else {
    int flapH = (int)roundf(halfH2 * p);
    if (flapH > 0) {
      // Growing back out of the hinge - starts edge-on (shadow-tinted)
      // and brightens towards white as it swings down to rest.
      digitSpr.setTextColor(blend565(COL_FLIP_TEXT, COL_FLIP_SHADOW, 1.0f - p), COL_FLIP_CARD);
      digitSpr.setViewport(0, hingeY, CELL_DIGIT_W, flapH);
      digitSpr.drawString(String(toCh), CELL_DIGIT_W / 2, textCy);
      digitSpr.resetViewport();
    }
  }

  digitSpr.fillRect(cardX, hingeY - 1, cardW, 2, COL_FLIP_HINGE);
  drawFlipHingePins(cardX, cardW, hingeY);
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

// digitSpr holds Fredoka, the only smooth font any remaining face needs
// (the LED face doesn't use a font at all; Photo/Botanical draw from
// their own raw bitmaps via photoDigitSpr instead). Loaded once.
void ensureDigitFont() {
  static bool loaded = false;
  if (loaded) return;
  digitSpr.loadFont(FredokaDigits87);
  loaded = true;
}

// ---- Photo faces --------------------------------------------------------
// Three faces built from real digit images (RGB565), not a font: Photo
// (PhotoDigits.h, background removed, a font-sampler style - a different
// typeface/colour per digit value, several of them dark, so it needs a
// light background or half of them are near-invisible), Botanical
// (BotanicalDigits.h, illuminated-manuscript digits - their own black
// panel is part of the artwork, kept as-is rather than background-
// removed), and Silver (SilverDigits.h, chrome-gradient numerals with a
// soft glow, same black-backdrop-kept-as-is treatment as Botanical's).
// Each digit has a different native size, so unlike every other face
// these can't reuse the fixed CELL_DIGIT_W column grid: laid out edge to
// edge at their native aspect ratio, a full row would run well past this
// 320px screen. Every digit is instead scaled to a single fixed-width slot
// (photoSlotW, computed below as the widest digit across all three sets
// at PHOTO_H tall, so one shared size/layout serves all of them) so the
// whole row's width - and thus its centred x position - never changes
// between redraws; without that, the row would visibly jump sideways
// every second as narrower/wider digits rotated through.
//
// Shows all 6 digits (HH:MM:SS). At a fixed size that must never clip on
// any possible time, that caps PHOTO_H well below the cell height (140px)
// - it's bounded by the digits' own aspect ratio, not by how tight the
// gaps/colon widths get (tried several combinations; none clear ~53px,
// Botanical's "7" at ~0.91 being the tightest of the three sets' worst
// case - Silver's widest, "4" at ~0.81, doesn't change that bound).
const int PHOTO_GAP = 1;       // gap between adjacent digit/colon slots
const int PHOTO_COLON_W = 7;
const int PHOTO_H = 53;
int photoSlotW = 0;       // widest scaled digit (either set) - set in begin()
int photoRowStartX = 0;   // fixed row x so it never shifts between redraws
int photoRowY = 0;

// Which digit set/background/colon colour the current face uses. Photo's
// mixed set includes several dark digits (black/dark-brown/dark-navy)
// that would be near-invisible on black, so it gets a white background
// and dark colon dots; Botanical's digits already carry their own black
// panel, so they sit on the same deep green as their badges. Silver's
// digits carry their own black backdrop too (see SilverDigits.h), so like
// Botanical they sit on a matching background rather than white - here
// that's plain black, with light colon dots to read against it.
const PhotoDigit *activePhotoSet() {
  if (currentFace == FACE_BOTANICAL) return BOTANICAL_DIGITS;
  if (currentFace == FACE_SILVER) return SILVER_DIGITS;
  return PHOTO_DIGITS;
}
uint16_t activePhotoBg() {
  if (currentFace == FACE_BOTANICAL) return COL_BOTANICAL_BG;
  if (currentFace == FACE_SILVER) return COL_BG;
  return TFT_WHITE;
}
uint16_t activePhotoColonColor() {
  if (currentFace == FACE_BOTANICAL) return COL_BOTANICAL_TEXT;
  if (currentFace == FACE_SILVER) return COL_SILVER_TEXT;
  return TFT_BLACK;
}

// Scaled width of digit d (0-9) in the given set at a fixed height of
// PHOTO_H, preserving its native aspect ratio.
int photoScaledWidth(const PhotoDigit *set, int d) {
  const PhotoDigit &pd = set[d];
  return (pd.w * PHOTO_H + pd.h - 1) / pd.h; // ceil
}

// Nearest-neighbour scales digit d from its native PROGMEM bitmap into
// photoDigitSpr at (photoSlotW x PHOTO_H) - photoSlotW rather than just
// this digit's own scaled width, so a narrower digit's leftover slot
// space still gets a background pixel instead of being left undrawn.
void drawPhotoDigitToSprite(const PhotoDigit *set, int d, uint16_t bg) {
  const PhotoDigit &pd = set[d];
  int sw = photoScaledWidth(set, d);
  // Centred in the slot, not left-aligned: photoSlotW is sized for the
  // widest digit across all three sets, so a narrow one (Silver's "1" is
  // barely a quarter as wide as its own "4") left a lopsided gap of bare
  // background to its right instead of splitting it evenly on both sides -
  // most visible as uneven spacing between digits on hardware.
  int offset = (photoSlotW - sw) / 2;
  for (int y = 0; y < PHOTO_H; y++) {
    int sy = (y * pd.h) / PHOTO_H;
    const uint16_t *row = pd.data + (size_t)sy * pd.w;
    for (int x = 0; x < photoSlotW; x++) {
      if (x < offset || x >= offset + sw) {
        photoDigitSpr.drawPixel(x, y, bg);
        continue;
      }
      int sx = ((x - offset) * pd.w) / sw;
      photoDigitSpr.drawPixel(x, y, pgm_read_word(&row[sx]));
    }
  }
}

void drawPhotoColon(int x, bool visible, uint16_t bg, uint16_t dotColor) {
  tft.fillRect(x, photoRowY, PHOTO_COLON_W, PHOTO_H, bg);
  if (visible) {
    int cx = x + PHOTO_COLON_W / 2;
    int cy = photoRowY + PHOTO_H / 2;
    int r = max(3, PHOTO_COLON_W / 3);
    int gap = PHOTO_H / 5;
    tft.fillSmoothCircle(cx, cy - gap, r, dotColor, bg);
    tft.fillSmoothCircle(cx, cy + gap, r, dotColor, bg);
  }
}

// Redraws the whole HH:MM:SS row in one pass - unlike the other faces'
// per-cell diffing, every slot's x position depends on the fixed
// photoSlotW rather than that slot's own content, so there's nothing
// meaningful to diff per-digit; this just runs whenever any digit or the
// colon blink state changes (see update()).
void drawPhotoRow(const char *buf, bool colonVisible) {
  const PhotoDigit *set = activePhotoSet();
  uint16_t bg = activePhotoBg();
  uint16_t colonColor = activePhotoColonColor();
  tft.fillRect(0, CLOCK_TOP, SCR_W, CLOCK_H, bg);
  int x = photoRowStartX;
  int idx = 0;
  for (int slot = 0; slot < 8; slot++) {
    if (slot == 2 || slot == 5) {
      drawPhotoColon(x, colonVisible, bg, colonColor);
      x += PHOTO_COLON_W + PHOTO_GAP;
    } else {
      int d = buf[idx++] - '0';
      drawPhotoDigitToSprite(set, d, bg);
      photoDigitSpr.pushSprite(x, photoRowY);
      x += photoSlotW + PHOTO_GAP;
    }
  }
}

void drawDigitCell(int col, char ch) {
  if (currentFace == FACE_SEVEN_SEG) {
    drawSevenSegDigitCell(col, ch);
  } else if (currentFace == FACE_FLIP) {
    drawFlipDigitCell(col, ch);
  } else {
    drawRainbowGridDigitCell(col, ch);
  }
}

void drawColonCell(int col, bool visible) {
  int x = colX(col);
  uint16_t bg = (currentFace == FACE_FLIP) ? COL_FLIP_BG : COL_BG;
  colonSpr.fillSprite(bg);
  if (visible) {
    int cx = CELL_COLON_W / 2;
    int cy = CLOCK_H / 2;
    int r = max(3, CELL_COLON_W / 6);
    int gap = CLOCK_H / 6;
    colonSpr.fillSmoothCircle(cx, cy - gap, r, COL_COLON, bg);
    colonSpr.fillSmoothCircle(cx, cy + gap, r, COL_COLON, bg);
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

  // Gloss (Silver Face only) before the corner carve, same order the
  // rainbow grid digit cells use - see applyDigitGloss()'s own comment.
  if (badgeGlossActive()) applyDigitGloss(spr, b.w, TOPBAR_H);

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
  if (badgeGlossActive()) applyDigitGloss(mdaySpr, b.w, TOPBAR_H);
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
  if (badgeGlossActive()) applyDigitGloss(wifiSpr, b.w, TOPBAR_H);
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
  photoDigitSpr.setColorDepth(16);

  digitSpr.createSprite(CELL_DIGIT_W, CLOCK_H);
  colonSpr.createSprite(CELL_COLON_W, CLOCK_H);
  yearSpr.createSprite(B_YEAR.w, TOPBAR_H);
  mdaySpr.createSprite(B_MDAY.w, TOPBAR_H);
  weekSpr.createSprite(B_WEEK.w, TOPBAR_H);
  doySpr.createSprite(B_DOY.w, TOPBAR_H);
  wifiSpr.createSprite(B_WIFI.w, TOPBAR_H);

  for (int d = 0; d <= 9; d++) {
    photoSlotW = max(photoSlotW, photoScaledWidth(PHOTO_DIGITS, d));
    photoSlotW = max(photoSlotW, photoScaledWidth(BOTANICAL_DIGITS, d));
    photoSlotW = max(photoSlotW, photoScaledWidth(SILVER_DIGITS, d));
  }
  photoDigitSpr.createSprite(photoSlotW, PHOTO_H);
  int photoRowW = 6 * photoSlotW + 2 * PHOTO_COLON_W + 7 * PHOTO_GAP;
  photoRowStartX = max(0, (SCR_W - photoRowW) / 2);
  photoRowY = CLOCK_TOP + (CLOCK_H - PHOTO_H) / 2;

  ensureDigitFont(); // loads FredokaDigits87 for the default rainbow face

  drawGrid();
  gridDrawn = true;
}

// Restarts Video Face playback from frame 0 every time it's (re)entered,
// rather than resuming mid-clip.
void ensureVideoFaceEntered() {
  if (currentFace == FACE_VIDEO) VideoPlayer::reset();
}

// Cycles to the next/previous clock face and forces a full repaint on the
// next update() call, so the switch is visible right away instead of
// waiting for a digit to actually change.
void nextFace() {
  currentFace = (currentFace + 1) % FACE_COUNT;
  ensureVideoFaceEntered();
  gridDrawn = false;
}

void prevFace() {
  currentFace = (currentFace + FACE_COUNT - 1) % FACE_COUNT;
  ensureVideoFaceEntered();
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
    for (int i = 0; i < CELL_COUNT; i++) digitAnims[i].active = false;
  }

  // ---- clock digits ----
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d%02d%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  // buf: H H M M S S  -> map into the 8 cells (2 colon cells in between)
  // The colon dots blink once a second (on for even seconds, off for odd).
  int colonVisible = (timeinfo.tm_sec % 2 == 0) ? 1 : 0;
  if (currentFace == FACE_VIDEO) {
    // Video Face replaces the *entire* screen, status bar included - not
    // just the usual CLOCK_TOP..CLOCK_H digit area every other face
    // shares - for a fullscreen look with nothing overlaid on top. It
    // reads looping frames straight off the SD card (see video_player.h),
    // entirely independent of the web portal/WiFi, so it keeps playing
    // after the browser tab that uploaded it is closed. VideoPlayer::
    // draw() self-paces off the video's own saved fps and no-ops between
    // frames, so it's cheap to call on every tick regardless of this
    // face's usual per-second cadence.
    if (VideoPlayer::matchesSize(SCR_W, SCR_H)) {
      VideoPlayer::draw(tft, 0, 0, SCR_W, SCR_H);
    } else {
      // Two different reasons nothing plays: no video saved at all, or one
      // saved under an older firmware version's output size (draw() won't
      // stretch/crop to fit) - tell them apart rather than leaving a blank
      // screen with no explanation either way.
      tft.fillRect(0, 0, SCR_W, SCR_H, COL_BG);
      tft.setFreeFont(&FreeSansBold9pt7b);
      tft.setTextColor(TFT_WHITE, COL_BG);
      tft.setTextDatum(MC_DATUM);
      if (VideoPlayer::isAvailable()) {
        tft.drawString("Saved video is the wrong size", SCR_W / 2, SCR_H / 2 - 12);
        tft.drawString("Re-upload it from the web portal", SCR_W / 2, SCR_H / 2 + 12);
      } else {
        tft.drawString("No video saved", SCR_W / 2, SCR_H / 2 - 12);
        tft.drawString("Upload one from the web portal", SCR_W / 2, SCR_H / 2 + 12);
      }
      tft.setFreeFont(nullptr);
    }
  } else if (currentFace == FACE_PHOTO || currentFace == FACE_BOTANICAL || currentFace == FACE_SILVER) {
    // Every slot's x position is fixed (see drawPhotoRow()), so there's
    // nothing to diff per-digit - just redraw the whole row when anything
    // in it changed. lastDigit[0..5] doubles as this face's HHMMSS cache
    // (it's fully reset to 0 on every face switch, so it never carries
    // stale values over from the other faces' cell-indexed usage of it).
    bool changed = (colonVisible != lastColonVisible);
    for (int i = 0; i < 6; i++) {
      if (lastDigit[i] != buf[i]) changed = true;
    }
    if (changed) {
      drawPhotoRow(buf, colonVisible);
      for (int i = 0; i < 6; i++) lastDigit[i] = buf[i];
    }
  } else {
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
      bool animatedFace = (currentFace == FACE_RAINBOW_GRID || currentFace == FACE_FLIP);
      if (animatedFace) {
        if (lastDigit[col] != ch) {
          if (lastDigit[col] == 0) {
            // First draw for this cell (just reset/switched to) - no
            // previous digit to animate from, so draw it directly.
            drawDigitCell(col, ch);
          } else {
            digitAnims[col] = {lastDigit[col], ch, millis(), true};
          }
          lastDigit[col] = ch;
        }
        if (digitAnims[col].active) {
          unsigned long durMs = (currentFace == FACE_FLIP) ? FLIP_ANIM_MS : DIGIT_ANIM_MS;
          unsigned long elapsed = millis() - digitAnims[col].startMs;
          if (elapsed >= durMs) {
            digitAnims[col].active = false;
            drawDigitCell(col, digitAnims[col].toCh);
          } else if (currentFace == FACE_FLIP) {
            drawFlipDigitCellAnimated(col, digitAnims[col].fromCh, digitAnims[col].toCh,
                                       (float)elapsed / durMs);
          } else {
            drawRainbowGridDigitCellAnimated(col, digitAnims[col].fromCh, digitAnims[col].toCh,
                                              (float)elapsed / durMs);
          }
        }
      } else if (lastDigit[col] != ch) {
        drawDigitCell(col, ch);
        lastDigit[col] = ch;
      }
    }
  }
  lastColonVisible = colonVisible;

  // Video Face uses the full screen (see its branch above, which draws
  // 0..SCR_H rather than the usual CLOCK_TOP..CLOCK_H) instead of the
  // status bar + digit area every other face shares, so none of that
  // applies while it's active.
  if (currentFace == FACE_VIDEO) return;

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
