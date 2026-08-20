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
const uint16_t COL_SETTINGS_ACCENT = rgb565(120, 200, 255); // WiFi/Time Zone tiles
const uint16_t COL_WARN      = rgb565(255, 90, 90);

// ---- menu state -------------------------------------------------------
enum State { ST_CLOCK, ST_MAIN, ST_CONTINENT, ST_ZONE, ST_WIFI_CONFIRM, ST_SAVED };
State state = ST_CLOCK;

const char *const MAIN_ITEMS[2] = {"WiFi Setup", "Time Zone"};
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

void render() {
  switch (state) {
    case ST_MAIN: {
      String pos = String(mainIndex + 1) + " / " + String((int)(sizeof(MAIN_ITEMS) / sizeof(MAIN_ITEMS[0])));
      drawScreen("SETTINGS", COL_HEADING_BG, COL_HEADING_TXT,
                 MAIN_ITEMS[mainIndex], COL_SETTINGS_ACCENT, pos, HINT_NAV);
      break;
    }
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
      const int n = sizeof(MAIN_ITEMS) / sizeof(MAIN_ITEMS[0]);
      if (leftTap) { mainIndex = (mainIndex + n - 1) % n; dirty = true; }
      if (rightTap) { mainIndex = (mainIndex + 1) % n; dirty = true; }
      if (okLong) exitToClock();
      else if (okTap) {
        if (mainIndex == 0) {
          state = ST_WIFI_CONFIRM;
        } else {
          findCurrentZone(continentIndex, zoneIndex);
          state = ST_CONTINENT;
        }
        dirty = true;
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
