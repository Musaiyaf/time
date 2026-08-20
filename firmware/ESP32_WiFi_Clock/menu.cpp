#include "menu.h"
#include "config.h"
#include "clock_display.h"
#include "wifi_manager.h"
#include "tz_database.h"

namespace {

// ---- button debounce + tap/long-press detection --------------------------
const unsigned long DEBOUNCE_MS = 30;
const unsigned long LONG_PRESS_MS = 550;

struct Btn {
  int pin = -1;
  int stable = HIGH;
  int lastRead = HIGH;
  unsigned long lastChangeMs = 0;
  unsigned long downAtMs = 0;
  bool longFired = false;

  void begin(int p) {
    pin = p;
    pinMode(pin, INPUT_PULLUP);
  }

  // Sets tap=true on a completed short press-and-release, or
  // longPress=true once when the button has been held past LONG_PRESS_MS
  // (fires once while still held, so a long-press feels immediate rather
  // than waiting for release).
  void poll(bool &tap, bool &longPress) {
    tap = false;
    longPress = false;
    unsigned long now = millis();
    int r = digitalRead(pin);
    if (r != lastRead) {
      lastChangeMs = now;
      lastRead = r;
    }
    if (now - lastChangeMs > DEBOUNCE_MS && r != stable) {
      stable = r;
      if (stable == LOW) {
        downAtMs = now;
        longFired = false;
      } else if (!longFired && now - downAtMs < LONG_PRESS_MS) {
        tap = true;
      }
    }
    if (stable == LOW && !longFired && now - downAtMs >= LONG_PRESS_MS) {
      longFired = true;
      longPress = true;
    }
  }
};

Btn btnLeft, btnRight, btnOk;

// ---- colours --------------------------------------------------------------
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}
const uint16_t COL_BG        = 0x0000; // black
const uint16_t COL_HEADING_BG = rgb565(35, 38, 46);   // neutral dark grey
const uint16_t COL_HEADING_TXT = rgb565(210, 214, 222);
const uint16_t COL_ITEM_TXT_DIM = rgb565(140, 145, 155);
const uint16_t COL_HINT_TXT  = rgb565(120, 125, 135);
const uint16_t COL_SETTINGS_ACCENT = rgb565(120, 200, 255); // Time Zone continent/zone screens
const uint16_t COL_WARN      = rgb565(255, 90, 90);
const uint16_t COL_TILE_BORDER = rgb565(50, 54, 62);  // unselected tile outline

// Vibrant per-icon colours for the top-level tiles, reusing the same
// pink/orange/cyan family as the rainbow clock face for visual consistency
// across the whole firmware.
const uint16_t COL_ICON_WIFI = rgb565(0, 217, 255);   // cyan
const uint16_t COL_ICON_TZ   = rgb565(255, 159, 28);  // orange
const uint16_t COL_ICON_BACK = rgb565(255, 79, 163);  // pink

// ---- menu state -------------------------------------------------------
enum State { ST_CLOCK, ST_MAIN, ST_CONTINENT, ST_ZONE, ST_WIFI_CONFIRM, ST_SAVED };
State state = ST_CLOCK;

// Top-level menu tiles: WiFi Setup, Time Zone, Back (exits to the clock
// face - the same thing holding OK does, but selectable directly too).
const int MAIN_TILE_COUNT = 3;
const char *const MAIN_LABELS[MAIN_TILE_COUNT] = {"WiFi", "Time Zone", "Back"};
const uint16_t MAIN_COLORS[MAIN_TILE_COUNT] = {COL_ICON_WIFI, COL_ICON_TZ, COL_ICON_BACK};
int mainIndex = 0;
int continentIndex = 0;
int zoneIndex = 0; // index within TZ_ZONES for the current continent
String savedZoneName;
unsigned long savedUntilMs = 0;

bool dirty = true; // true when the current screen needs (re)drawing

// Finds where the currently-saved TZ sits in the table, so opening the
// Time Zone menu starts on the zone that's actually active instead of
// always resetting to Africa/Abidjan. Falls back to (0, 0) if the saved
// POSIX string isn't an exact match (e.g. it was hand-edited).
void findCurrentZone(int &outContinent, int &outZoneIndex) {
  String tz, ntp1, ntp2;
  WifiManager::loadTimeConfig(tz, ntp1, ntp2);
  for (int i = 0; i < TZ_ZONE_COUNT; i++) {
    if (tz == TZ_ZONES[i].posix) {
      outContinent = TZ_ZONES[i].continent;
      outZoneIndex = i - TZ_CONTINENT_START[outContinent];
      return;
    }
  }
  outContinent = 0;
  outZoneIndex = 0;
}

// ---- rendering ----------------------------------------------------------
// One consistent screen layout, reused for every menu level: a coloured
// heading bar, a big centred item name, a small position indicator, and a
// bottom hint line describing what the buttons do right now.
void drawScreen(const String &heading, uint16_t headingBg, uint16_t headingTxt,
                 const String &item, uint16_t itemColor,
                 const String &position, const String &hint) {
  TFT_eSPI &tft = ClockDisplay::rawDisplay();
  const int W = TFT_SCREEN_WIDTH;
  const int cx = W / 2;
  tft.fillScreen(COL_BG);

  tft.fillRect(0, 0, W, 26, headingBg);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(headingTxt, headingBg);
  tft.drawString(heading, cx, 13);

  // 9pt, not 12pt: the longest zone names (e.g. "Argentina/Buenos Aires",
  // 22 chars) get close to the 320px screen width at 12pt bold and risk
  // clipping at the edges. 9pt is comfortably safe for every entry in
  // tz_database.h and still reads clearly at this size.
  tft.setFreeFont(&FreeSansBold12pt7b);
  int w = tft.textWidth(item);
  if (w > TFT_SCREEN_WIDTH - 20) tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(itemColor, COL_BG);
  tft.drawString(item, cx, 82);

  if (position.length()) {
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextColor(COL_ITEM_TXT_DIM, COL_BG);
    tft.drawString(position, cx, 118);
  }

  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(COL_HINT_TXT, COL_BG);
  tft.drawString(hint, cx, 154);

  tft.setFreeFont(nullptr);
}

const char *HINT_NAV = "< > select   OK confirm   hold back";

// ---- top-level menu icons -------------------------------------------
// Drawn as filled rings + simple strokes rather than 1px outlines, so they
// read as bold/vibrant even at this small size - no image assets, just the
// same primitive-shape approach used everywhere else in this firmware.
void iconRing(TFT_eSPI &tft, int cx, int cy, int r, int thickness, uint16_t color) {
  tft.fillCircle(cx, cy, r, color);
  tft.fillCircle(cx, cy, r - thickness, COL_BG);
}

void iconWifi(TFT_eSPI &tft, int cx, int cy, uint16_t color) {
  int baseY = cy + 14;
  for (int i = 0; i < 4; i++) {
    int h = 7 + i * 5;
    tft.fillRoundRect(cx - 22 + i * 12, baseY - h, 7, h, 2, color);
  }
}

// A small "globe" for Time Zone: a bold ring with an equator and a prime
// meridian through it, evoking world regions rather than a literal clock.
void iconGlobe(TFT_eSPI &tft, int cx, int cy, uint16_t color) {
  iconRing(tft, cx, cy, 15, 3, color);
  tft.drawFastHLine(cx - 15, cy, 30, color);
  tft.drawFastVLine(cx, cy - 15, 30, color);
}

// An analogue clock face for "Back" (return to the clock face).
void iconClock(TFT_eSPI &tft, int cx, int cy, uint16_t color) {
  iconRing(tft, cx, cy, 15, 3, color);
  tft.drawLine(cx, cy, cx, cy - 10, color);
  tft.drawLine(cx, cy, cx + 8, cy + 3, color);
  tft.fillCircle(cx, cy, 2, color);
}

void drawMainTiles() {
  TFT_eSPI &tft = ClockDisplay::rawDisplay();
  tft.fillScreen(COL_BG);

  const int tileW = 92, tileH = 100, gap = 8;
  const int totalW = tileW * MAIN_TILE_COUNT + gap * (MAIN_TILE_COUNT - 1);
  const int x0 = (TFT_SCREEN_WIDTH - totalW) / 2;
  const int y0 = 14;

  tft.setTextDatum(MC_DATUM);
  for (int i = 0; i < MAIN_TILE_COUNT; i++) {
    int x = x0 + i * (tileW + gap);
    bool sel = (i == mainIndex);
    uint16_t border = sel ? TFT_WHITE : COL_TILE_BORDER;
    tft.drawRoundRect(x, y0, tileW, tileH, 12, border);
    if (sel) tft.drawRoundRect(x + 1, y0 + 1, tileW - 2, tileH - 2, 11, border);

    int cx = x + tileW / 2, cy = y0 + 36;
    switch (i) {
      case 0: iconWifi(tft, cx, cy, MAIN_COLORS[i]); break;
      case 1: iconGlobe(tft, cx, cy, MAIN_COLORS[i]); break;
      default: iconClock(tft, cx, cy, MAIN_COLORS[i]); break;
    }

    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextColor(sel ? TFT_WHITE : COL_ITEM_TXT_DIM, COL_BG);
    tft.drawString(MAIN_LABELS[i], cx, y0 + tileH - 18);
  }

  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(COL_HINT_TXT, COL_BG);
  tft.drawString(HINT_NAV, TFT_SCREEN_WIDTH / 2, 154);
  tft.setFreeFont(nullptr);
}

void render() {
  switch (state) {
    case ST_MAIN:
      drawMainTiles();
      break;
    case ST_CONTINENT: {
      String pos = String(continentIndex + 1) + " / " + String(TZ_CONTINENT_COUNT);
      uint16_t accent = TZ_CONTINENT_COLORS[continentIndex];
      drawScreen("TIME ZONE - REGION", COL_HEADING_BG, COL_HEADING_TXT,
                 TZ_CONTINENT_NAMES[continentIndex], accent, pos, HINT_NAV);
      break;
    }
    case ST_ZONE: {
      int count = TZ_CONTINENT_ZONE_COUNT[continentIndex];
      const TzZone &z = TZ_ZONES[TZ_CONTINENT_START[continentIndex] + zoneIndex];
      String pos = String(zoneIndex + 1) + " / " + String(count);
      uint16_t accent = TZ_CONTINENT_COLORS[continentIndex];
      drawScreen(TZ_CONTINENT_NAMES[continentIndex], accent, COL_BG,
                 z.name, accent, pos, HINT_NAV);
      break;
    }
    case ST_WIFI_CONFIRM: {
      drawScreen("WIFI SETUP", COL_WARN, COL_BG,
                 "Reset & restart?", COL_WARN, "",
                 "OK confirm   hold cancel");
      break;
    }
    case ST_SAVED: {
      drawScreen("TIME ZONE", COL_HEADING_BG, COL_HEADING_TXT,
                 "Saved: " + savedZoneName, COL_SETTINGS_ACCENT, "", "");
      break;
    }
    default:
      break;
  }
}

void enterMain() {
  state = ST_MAIN;
  mainIndex = 0;
  dirty = true;
}

void exitToClock() {
  state = ST_CLOCK;
  ClockDisplay::forceFullRedraw();
}

} // namespace

namespace Menu {

void begin() {
  btnLeft.begin(BTN_LEFT_PIN);
  btnRight.begin(BTN_RIGHT_PIN);
  btnOk.begin(BTN_OK_PIN);
}

bool handle() {
  bool leftTap, leftLong, rightTap, rightLong, okTap, okLong;
  btnLeft.poll(leftTap, leftLong);
  btnRight.poll(rightTap, rightLong);
  btnOk.poll(okTap, okLong);

  switch (state) {
    case ST_CLOCK:
      if (leftTap) ClockDisplay::prevFace();
      if (rightTap) ClockDisplay::nextFace();
      if (okLong) enterMain();
      break;

    case ST_MAIN: {
      const int n = MAIN_TILE_COUNT;
      if (leftTap) { mainIndex = (mainIndex + n - 1) % n; dirty = true; }
      if (rightTap) { mainIndex = (mainIndex + 1) % n; dirty = true; }
      if (okLong) exitToClock();
      else if (okTap) {
        if (mainIndex == 0) {
          state = ST_WIFI_CONFIRM;
          dirty = true;
        } else if (mainIndex == 1) {
          findCurrentZone(continentIndex, zoneIndex);
          state = ST_CONTINENT;
          dirty = true;
        } else {
          exitToClock(); // "Back" tile - same as holding OK
        }
      }
      break;
    }

    case ST_CONTINENT:
      if (leftTap) { continentIndex = (continentIndex + TZ_CONTINENT_COUNT - 1) % TZ_CONTINENT_COUNT; dirty = true; }
      if (rightTap) { continentIndex = (continentIndex + 1) % TZ_CONTINENT_COUNT; dirty = true; }
      if (okLong) { state = ST_MAIN; dirty = true; }
      else if (okTap) {
        // Keep the preselected zone only if we're still on the continent
        // findCurrentZone() matched; otherwise start at the first zone.
        int matchContinent, matchZone;
        findCurrentZone(matchContinent, matchZone);
        zoneIndex = (continentIndex == matchContinent) ? matchZone : 0;
        state = ST_ZONE;
        dirty = true;
      }
      break;

    case ST_ZONE: {
      int count = TZ_CONTINENT_ZONE_COUNT[continentIndex];
      if (leftTap) { zoneIndex = (zoneIndex + count - 1) % count; dirty = true; }
      if (rightTap) { zoneIndex = (zoneIndex + 1) % count; dirty = true; }
      if (okLong) { state = ST_CONTINENT; dirty = true; }
      else if (okTap) {
        const TzZone &z = TZ_ZONES[TZ_CONTINENT_START[continentIndex] + zoneIndex];
        String tz, ntp1, ntp2;
        WifiManager::loadTimeConfig(tz, ntp1, ntp2);
        WifiManager::saveTimeConfig(String(z.posix), ntp1, ntp2);
        WifiManager::syncTime();
        savedZoneName = String(z.name);
        savedUntilMs = millis() + 1400;
        state = ST_SAVED;
        dirty = true;
      }
      break;
    }

    case ST_WIFI_CONFIRM:
      if (okLong) { state = ST_MAIN; dirty = true; }
      else if (okTap) {
        WifiManager::clearCredentials();
        ESP.restart();
      }
      break;

    case ST_SAVED:
      if (millis() >= savedUntilMs) exitToClock();
      break;
  }

  if (state != ST_CLOCK) {
    if (dirty) {
      render();
      dirty = false;
    }
    return true;
  }
  return false;
}

} // namespace Menu
