#include "menu.h"
#include "config.h"
#include "clock_display.h"
#include "wifi_manager.h"
#include "tz_database.h"
#include "sd_card.h"
#include <WiFi.h>

namespace {

// ---- button debounce + tap/long-press detection --------------------------
const unsigned long DEBOUNCE_MS = 30;
const unsigned long LONG_PRESS_MS = 550;

// Set by a hardware interrupt on each button's falling edge (see Btn::
// begin()) - catches a press even if loop() is busy and doesn't get back
// around to polling until well after the button's already been released
// again. Needed because a single Video Face frame (SD read + redraw) can
// block loop() for longer than a quick tap takes; without this, a tap
// that started and finished entirely inside that blocked stretch was
// never visible to plain digitalRead() polling at all - not delayed,
// just gone, which is what made the buttons feel dead while a video
// played. IRAM_ATTR keeps the ISR in internal RAM, required on the
// ESP32 for code that can run while flash access is busy elsewhere.
volatile bool leftEdgeFlag = false;
volatile bool rightEdgeFlag = false;
volatile bool okEdgeFlag = false;
void IRAM_ATTR isrLeftEdge() { leftEdgeFlag = true; }
void IRAM_ATTR isrRightEdge() { rightEdgeFlag = true; }
void IRAM_ATTR isrOkEdge() { okEdgeFlag = true; }

struct Btn {
  int pin = -1;
  int stable = HIGH;
  int lastRead = HIGH;
  unsigned long lastChangeMs = 0;
  unsigned long downAtMs = 0;
  bool longFired = false;
  volatile bool *edgeFlag = nullptr;

  void begin(int p, volatile bool *flag, void (*isr)()) {
    pin = p;
    pinMode(pin, INPUT_PULLUP);
    edgeFlag = flag;
    attachInterrupt(digitalPinToInterrupt(pin), isr, FALLING);
  }

  // Sets tap=true on a completed short press-and-release, or
  // longPress=true once when the button has been held past LONG_PRESS_MS
  // (fires once while still held, so a long-press feels immediate rather
  // than waiting for release).
  void poll(bool &tap, bool &longPress) {
    tap = false;
    longPress = false;
    unsigned long now = millis();

    // A falling edge the interrupt caught, but the pin already reads HIGH
    // (released) again and the debounced state machine below still thinks
    // it's HIGH too (never saw the press) - the whole tap happened between
    // two poll() calls. Synthesize it now rather than lose it. Only
    // affects tap detection; a genuine hold is still observed live by the
    // digitalRead() path below within its 550ms threshold, far longer
    // than any single blocking video frame, so long-press needs no
    // interrupt help.
    bool edgeMissed = false;
    if (edgeFlag) {
      noInterrupts();
      edgeMissed = *edgeFlag;
      *edgeFlag = false;
      interrupts();
    }

    int r = digitalRead(pin);
    if (edgeMissed && r == HIGH && stable == HIGH) {
      tap = true;
      return;
    }

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

// Blocks until OK is tapped (returns true) or held long (returns false) -
// used by the WiFi picker's own message screens ("No networks found",
// "Connect failed", ...), which live outside the async Menu::handle()
// state machine.
bool blockForOk() {
  while (true) {
    bool t, l;
    btnOk.poll(t, l);
    if (t) return true;
    if (l) return false;
    delay(10);
  }
}

// ---- colours --------------------------------------------------------------
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}
const uint16_t COL_BG        = 0x0000; // black
const uint16_t COL_HEADING_BG = rgb565(35, 38, 46);   // neutral dark grey
const uint16_t COL_HEADING_TXT = rgb565(210, 214, 222);
const uint16_t COL_ITEM_TXT_DIM = rgb565(140, 145, 155);
const uint16_t COL_HINT_TXT  = rgb565(120, 125, 135);
const uint16_t COL_SETTINGS_ACCENT = rgb565(120, 200, 255); // gear icon, Time Zone screens, About
const uint16_t COL_WARN      = rgb565(255, 90, 90);
const uint16_t COL_TILE_BORDER = rgb565(50, 54, 62);  // unselected tile outline

// Vibrant per-item colours, reusing the same pink/orange/cyan family as the
// rainbow clock face for visual consistency across the whole firmware.
const uint16_t COL_ICON_WIFI = rgb565(0, 217, 255);   // cyan
const uint16_t COL_ICON_TZ   = rgb565(255, 159, 28);  // orange
const uint16_t COL_ICON_BACK = rgb565(255, 79, 163);  // pink
const uint16_t COL_ICON_DATETIME = rgb565(140, 255, 150); // mint green
const uint16_t COL_ICON_SD = rgb565(255, 210, 60);    // gold

// ---- menu state -------------------------------------------------------
// CLOCK -> MAIN (3 icon tiles: SD Card, Settings, Back) -> SETTINGS (text
// list: WiFi, Time Zone, Date/Time, About) -> CONTINENT -> ZONE, or ->
// ABOUT. SD Card, WiFi and Date/Time don't get their own states -
// selecting them runs the blocking runSdBrowser(), runWifiPicker() (scan
// -> pick network -> type password -> connect) or runDateTimeSetter()
// flow and returns straight back to MAIN/SETTINGS.
enum State { ST_CLOCK, ST_MAIN, ST_SETTINGS, ST_CONTINENT, ST_ZONE, ST_ABOUT, ST_SAVED };
State state = ST_CLOCK;

const int MAIN_TILE_COUNT = 3;
const char *const MAIN_LABELS[MAIN_TILE_COUNT] = {"SD Card", "Settings", "Back"};
const uint16_t MAIN_COLORS[MAIN_TILE_COUNT] = {COL_ICON_SD, COL_SETTINGS_ACCENT, COL_ICON_BACK};
int mainIndex = 0;

const int SETTINGS_COUNT = 4;
const char *const SETTINGS_LABELS[SETTINGS_COUNT] = {"WiFi", "Time Zone", "Date/Time", "About"};
const uint16_t SETTINGS_COLORS[SETTINGS_COUNT] = {COL_ICON_WIFI, COL_ICON_TZ, COL_ICON_DATETIME, COL_SETTINGS_ACCENT};
int settingsIndex = 0;

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
// One consistent screen layout, reused for every text-based menu level: a
// coloured heading bar, a big centred item name, a small position
// indicator, and a bottom hint line describing what the buttons do now.
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

// A gear for "Settings".
void iconGear(TFT_eSPI &tft, int cx, int cy, uint16_t color) {
  iconRing(tft, cx, cy, 12, 3, color);
  tft.fillRect(cx - 3, cy - 17, 6, 5, color);  // top tooth
  tft.fillRect(cx - 3, cy + 12, 6, 5, color);  // bottom tooth
  tft.fillRect(cx - 17, cy - 3, 5, 6, color);  // left tooth
  tft.fillRect(cx + 12, cy - 3, 5, 6, color);  // right tooth
  tft.fillCircle(cx, cy, 4, color);            // hub
}

// An analogue clock face for "Back" (return to the clock face).
void iconClock(TFT_eSPI &tft, int cx, int cy, uint16_t color) {
  iconRing(tft, cx, cy, 15, 3, color);
  tft.drawLine(cx, cy, cx, cy - 10, color);
  tft.drawLine(cx, cy, cx + 8, cy + 3, color);
  tft.fillCircle(cx, cy, 2, color);
}

// A memory-card silhouette for "SD Card": a rounded rect with a notched
// corner and three short "contact" marks near the bottom.
void iconSdCard(TFT_eSPI &tft, int cx, int cy, uint16_t color) {
  const int w = 22, h = 28;
  int x = cx - w / 2, y = cy - h / 2;
  tft.fillRoundRect(x, y, w, h, 3, color);
  tft.fillTriangle(x, y, x + 9, y, x, y + 9, COL_BG); // notch the top-left corner
  for (int i = 0; i < 3; i++) {
    tft.fillRect(x + 4 + i * 6, y + h - 9, 3, 6, COL_BG);
  }
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
    if (i == 0) iconSdCard(tft, cx, cy, MAIN_COLORS[i]);
    else if (i == 1) iconGear(tft, cx, cy, MAIN_COLORS[i]);
    else iconClock(tft, cx, cy, MAIN_COLORS[i]);

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
    case ST_SETTINGS: {
      String pos = String(settingsIndex + 1) + " / " + String(SETTINGS_COUNT);
      drawScreen("SETTINGS", COL_HEADING_BG, COL_HEADING_TXT,
                 SETTINGS_LABELS[settingsIndex], SETTINGS_COLORS[settingsIndex], pos, HINT_NAV);
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
    case ST_ABOUT: {
      bool up = WiFi.status() == WL_CONNECTED;
      String item = up ? WiFi.localIP().toString() : "Offline";
      String position = up ? String(MDNS_HOSTNAME) + ".local" : "No WiFi connection";
      drawScreen("ABOUT", COL_HEADING_BG, COL_HEADING_TXT,
                 item, COL_SETTINGS_ACCENT, position, "OK or hold: back");
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

// ---- WiFi onboarding: scan -> pick network -> type password -> connect --
// Fully blocking (its own button-poll loop), since it's a focused task the
// rest of the UI naturally pauses for - same as the original AP-mode setup
// screen already did. Runs from the Settings menu, and also from setup()
// in the .ino when auto-connecting to the saved network fails.

// On-screen "keyboard": one character (or a control action) at a time,
// cycled with LEFT/RIGHT and appended with a tap of OK - the same
// single-item-carousel interaction used everywhere else in this menu,
// just applied to characters instead of menu items. There's no way to
// avoid this being tedious with only two navigation buttons; the row is
// ordered lowercase-first since most passwords lean that way.
const char *const WIFI_CHARSET =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    " !@#$%^&*()-_=+.,";
const char *const WIFI_CTRL_LABELS[3] = {"DELETE", "CONNECT", "CANCEL"};
const int WIFI_CTRL_DELETE = 0, WIFI_CTRL_CONNECT = 1, WIFI_CTRL_CANCEL = 2;

// Returns true and fills outPassword if the user picked CONNECT; false if
// they picked CANCEL or held OK.
bool runPasswordEntry(const String &ssid, String &outPassword) {
  int charsetLen = strlen(WIFI_CHARSET);
  int totalPositions = charsetLen + 3;

  String pw = "";
  int pos = 0;
  bool dirtyLocal = true;

  while (true) {
    bool lt, ll, rt, rl, ot, ol;
    btnLeft.poll(lt, ll);
    btnRight.poll(rt, rl);
    btnOk.poll(ot, ol);

    if (lt) { pos = (pos + totalPositions - 1) % totalPositions; dirtyLocal = true; }
    if (rt) { pos = (pos + 1) % totalPositions; dirtyLocal = true; }
    if (ol) return false; // hold OK: cancel entirely, back to the network list
    if (ot) {
      if (pos < charsetLen) {
        pw += WIFI_CHARSET[pos];
        dirtyLocal = true;
      } else {
        int ctrl = pos - charsetLen;
        if (ctrl == WIFI_CTRL_DELETE) {
          if (pw.length() > 0) pw.remove(pw.length() - 1);
          dirtyLocal = true;
        } else if (ctrl == WIFI_CTRL_CONNECT) {
          outPassword = pw;
          return true;
        } else {
          return false; // CANCEL
        }
      }
    }

    if (dirtyLocal) {
      String itemLabel;
      if (pos < charsetLen) {
        char c = WIFI_CHARSET[pos];
        itemLabel = (c == ' ') ? String("SPACE") : String(c);
      } else {
        itemLabel = WIFI_CTRL_LABELS[pos - charsetLen];
      }
      String shown = pw.length() ? pw : String("(empty)");
      if (shown.length() > 24) shown = "..." + shown.substring(shown.length() - 21);
      drawScreen(ssid, COL_HEADING_BG, COL_HEADING_TXT, itemLabel, COL_SETTINGS_ACCENT,
                 shown, "< > char   OK pick   hold cancel");
      dirtyLocal = false;
    }
    delay(5);
  }
}

const int WIFI_MAX_NETWORKS = 30;

// ---- Date/Time setter ------------------------------------------------
// Sets the clock by hand, one field at a time (Year -> Month -> Day ->
// Hour -> Minute), the same carousel interaction as everywhere else:
// LEFT/RIGHT changes the highlighted field's value, a tap of OK moves to
// the next one. Confirming Minute applies the new time immediately;
// holding OK at any point cancels without changing anything. This is how
// Manual (offline) mode's placeholder clock gets corrected, and also
// works anytime to nudge the time while connected.
const char *const DT_MONTH_NAMES[12] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December"};

int daysInMonth(int year, int month) {
  static const int base[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    return leap ? 29 : 28;
  }
  return base[month - 1];
}

int wrapValue(int v, int lo, int hi) {
  int range = hi - lo + 1;
  return lo + ((v - lo) % range + range) % range;
}

String pad2(int v) {
  String s = String(v);
  return s.length() < 2 ? "0" + s : s;
}

void runDateTimeSetter() {
  struct tm now;
  bool haveNow = getLocalTime(&now, 200);
  int year = haveNow ? now.tm_year + 1900 : 2026;
  int month = haveNow ? now.tm_mon + 1 : 1;
  int day = haveNow ? now.tm_mday : 1;
  int hour = haveNow ? now.tm_hour : 0;
  int minute = haveNow ? now.tm_min : 0;

  enum Field { F_YEAR, F_MONTH, F_DAY, F_HOUR, F_MINUTE, F_COUNT };
  const char *const FIELD_NAMES[F_COUNT] = {"YEAR", "MONTH", "DAY", "HOUR", "MINUTE"};
  int field = F_YEAR;
  bool dirtyLocal = true;

  while (true) {
    bool lt, ll, rt, rl, ot, ol;
    btnLeft.poll(lt, ll);
    btnRight.poll(rt, rl);
    btnOk.poll(ot, ol);

    if (ol) return; // hold OK: cancel, discard changes

    day = min(day, daysInMonth(year, month));

    if (lt || rt) {
      int delta = rt ? 1 : -1;
      switch (field) {
        case F_YEAR:   year = wrapValue(year + delta, 2000, 2099); break;
        case F_MONTH:  month = wrapValue(month + delta, 1, 12); break;
        case F_DAY:    day = wrapValue(day + delta, 1, daysInMonth(year, month)); break;
        case F_HOUR:   hour = wrapValue(hour + delta, 0, 23); break;
        case F_MINUTE: minute = wrapValue(minute + delta, 0, 59); break;
      }
      dirtyLocal = true;
    }

    if (ot) {
      if (field == F_MINUTE) {
        WifiManager::setManualDateTime(year, month, day, hour, minute);
        return;
      }
      field++;
      dirtyLocal = true;
    }

    if (dirtyLocal) {
      String valueText;
      if (field == F_YEAR) valueText = String(year);
      else if (field == F_MONTH) valueText = DT_MONTH_NAMES[month - 1];
      else if (field == F_DAY) valueText = String(day);
      else if (field == F_HOUR) valueText = pad2(hour);
      else valueText = pad2(minute);

      String preview = String(year) + "-" + pad2(month) + "-" + pad2(day) +
                        "  " + pad2(hour) + ":" + pad2(minute);
      drawScreen(String("SET ") + FIELD_NAMES[field], COL_HEADING_BG, COL_HEADING_TXT,
                 valueText, COL_ICON_DATETIME, preview, "< > change   OK next   hold cancel");
      dirtyLocal = false;
    }
    delay(5);
  }
}

// ---- SD card browser ---------------------------------------------------
// Read-only: lists files/folders and lets you drill into directories or
// "view" a file's name and exact size. This firmware has no text/image
// viewer, so opening a file just shows those details rather than its
// contents. Fully blocking, same pattern as the WiFi picker/Date-Time
// setter above.
const int SD_MAX_ENTRIES = 40;

// Human-friendly size, since raw byte counts get long fast on real cards.
String formatSize(uint32_t bytes) {
  if (bytes < 1024) return String(bytes) + "B";
  if (bytes < 1024UL * 1024) return String(bytes / 1024.0, 1) + "KB";
  return String(bytes / (1024.0 * 1024), 1) + "MB";
}

String sdChildPath(const String &parent, const String &name) {
  return (parent == "/") ? "/" + name : parent + "/" + name;
}

String sdParentPath(const String &path) {
  if (path == "/") return "/";
  int slash = path.lastIndexOf('/');
  return (slash <= 0) ? "/" : path.substring(0, slash);
}

// A real scrollable list (several entries on screen at once, like a phone's
// file browser) rather than one-entry-at-a-time - the SD card is the one
// place in this menu where you might be picking from dozens of entries, so
// stepping through them one by one doesn't scale the way it does for a
// handful of menu items.
const int SD_ROW_H = 19;
const int SD_LIST_TOP = 28;
const int SD_VISIBLE_ROWS = (TFT_SCREEN_HEIGHT - SD_LIST_TOP - 20) / SD_ROW_H;

void drawSdList(const String &path, SdCard::Entry *entries, int count, int idx,
                 const char *hint = "< > move   OK open   hold back") {
  TFT_eSPI &tft = ClockDisplay::rawDisplay();
  tft.fillScreen(COL_BG);

  tft.fillRect(0, 0, TFT_SCREEN_WIDTH, 26, COL_HEADING_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(COL_HEADING_TXT, COL_HEADING_BG);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(path, TFT_SCREEN_WIDTH / 2, 13);
  if (count > 0) {
    tft.setTextDatum(MR_DATUM);
    tft.drawString(String(idx + 1) + "/" + String(count), TFT_SCREEN_WIDTH - 6, 13);
  }

  if (count == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_ITEM_TXT_DIM, COL_BG);
    tft.drawString("(empty)", TFT_SCREEN_WIDTH / 2, SD_LIST_TOP + 40);
  } else {
    int windowStart = 0;
    if (count > SD_VISIBLE_ROWS) {
      windowStart = idx - SD_VISIBLE_ROWS / 2;
      windowStart = max(0, min(windowStart, count - SD_VISIBLE_ROWS));
    }
    int rowsToShow = min(count - windowStart, SD_VISIBLE_ROWS);

    for (int i = 0; i < rowsToShow; i++) {
      int entryIdx = windowStart + i;
      int y = SD_LIST_TOP + i * SD_ROW_H;
      bool sel = (entryIdx == idx);
      const SdCard::Entry &e = entries[entryIdx];
      uint16_t rowBg = sel ? COL_ICON_SD : COL_BG;
      uint16_t txtColor = sel ? COL_BG : (e.isDir ? TFT_WHITE : COL_ITEM_TXT_DIM);

      if (sel) tft.fillRect(0, y, TFT_SCREEN_WIDTH, SD_ROW_H, rowBg);
      tft.setTextColor(txtColor, rowBg);
      tft.setTextDatum(ML_DATUM);
      tft.drawString(e.isDir ? ("[DIR] " + e.name) : e.name, 8, y + SD_ROW_H / 2);
      if (!e.isDir) {
        tft.setTextDatum(MR_DATUM);
        tft.drawString(formatSize(e.size), TFT_SCREEN_WIDTH - 8, y + SD_ROW_H / 2);
      }
    }
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_HINT_TXT, COL_BG);
  tft.drawString(hint, TFT_SCREEN_WIDTH / 2, TFT_SCREEN_HEIGHT - 10);
  tft.setFreeFont(nullptr);
}

void runSdBrowser() {
  if (!SdCard::isPresent()) {
    drawScreen("SD CARD", COL_WARN, COL_BG, "No SD card found", COL_WARN, "", "tap OK to go back");
    blockForOk();
    return;
  }

  String path = "/";
  static SdCard::Entry entries[SD_MAX_ENTRIES];
  int count = 0;
  int idx = 0;
  bool dirtyLocal = true;
  bool needReload = true;

  while (true) {
    if (needReload) {
      count = SdCard::listDir(path, entries, SD_MAX_ENTRIES);
      idx = 0;
      needReload = false;
      dirtyLocal = true;
    }

    bool lt, ll, rt, rl, ot, ol;
    btnLeft.poll(lt, ll);
    btnRight.poll(rt, rl);
    btnOk.poll(ot, ol);

    if (count > 0) {
      if (lt) { idx = (idx + count - 1) % count; dirtyLocal = true; }
      if (rt) { idx = (idx + 1) % count; dirtyLocal = true; }
    }

    if (ol) {
      if (path == "/") return; // back out of the browser entirely
      path = sdParentPath(path);
      needReload = true;
      continue;
    }

    if (ot && count > 0) {
      if (entries[idx].isDir) {
        path = sdChildPath(path, entries[idx].name);
        needReload = true;
        continue;
      }
      drawScreen("FILE", COL_HEADING_BG, COL_HEADING_TXT, entries[idx].name,
                 COL_ICON_SD, String(entries[idx].size) + " bytes", "tap or hold: back");
      blockForOk();
      dirtyLocal = true;
      continue;
    }

    if (dirtyLocal) {
      drawSdList(path, entries, count, idx);
      dirtyLocal = false;
    }
    delay(5);
  }
}

} // namespace

namespace Menu {

void begin() {
  btnLeft.begin(BTN_LEFT_PIN, &leftEdgeFlag, isrLeftEdge);
  btnRight.begin(BTN_RIGHT_PIN, &rightEdgeFlag, isrRightEdge);
  btnOk.begin(BTN_OK_PIN, &okEdgeFlag, isrOkEdge);
}

bool runWifiPicker() {
  drawScreen("WIFI SETUP", COL_HEADING_BG, COL_HEADING_TXT, "Scanning...", COL_SETTINGS_ACCENT, "", "");
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    drawScreen("WIFI SETUP", COL_WARN, COL_BG, "No networks found", COL_WARN, "", "tap OK to go back");
    blockForOk();
    WiFi.scanDelete();
    return false;
  }

  int count = min(n, WIFI_MAX_NETWORKS);
  String ssids[WIFI_MAX_NETWORKS];
  bool secured[WIFI_MAX_NETWORKS];
  for (int i = 0; i < count; i++) {
    ssids[i] = WiFi.SSID(i);
    secured[i] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();

  int idx = 0;
  bool dirtyLocal = true;
  while (true) {
    bool lt, ll, rt, rl, ot, ol;
    btnLeft.poll(lt, ll);
    btnRight.poll(rt, rl);
    btnOk.poll(ot, ol);

    if (lt) { idx = (idx + count - 1) % count; dirtyLocal = true; }
    if (rt) { idx = (idx + 1) % count; dirtyLocal = true; }
    if (ol) return false; // hold OK: cancel, back to Settings

    if (ot) {
      String pass;
      if (secured[idx]) {
        if (!runPasswordEntry(ssids[idx], pass)) {
          dirtyLocal = true;
          continue; // cancelled password entry - back to the network list
        }
      }
      drawScreen("WIFI SETUP", COL_HEADING_BG, COL_HEADING_TXT, "Connecting...", COL_SETTINGS_ACCENT, ssids[idx], "");
      bool ok = WifiManager::connectSTA(ssids[idx], pass, WIFI_CONNECT_TIMEOUT_MS);
      if (ok) {
        WifiManager::saveCredentials(ssids[idx], pass);
        WifiManager::startMDNS(); // IP likely changed - re-announce it
        WifiManager::syncTime();  // get real time over NTP on this network
        drawScreen("WIFI SETUP", COL_HEADING_BG, COL_HEADING_TXT, "Connected!", COL_SETTINGS_ACCENT, ssids[idx], "");
        delay(1200);
        return true;
      }
      drawScreen("WIFI SETUP", COL_WARN, COL_BG, "Connect failed", COL_WARN, ssids[idx], "OK: retry   hold: cancel");
      if (!blockForOk()) return false; // held OK: give up, back to Settings
      dirtyLocal = true;
      continue; // tapped OK: back to the network list to try again
    }

    if (dirtyLocal) {
      String label = ssids[idx] + (secured[idx] ? "  [locked]" : "  [open]");
      String pos = String(idx + 1) + " / " + String(count);
      drawScreen("WIFI NETWORKS", COL_HEADING_BG, COL_HEADING_TXT, label, COL_SETTINGS_ACCENT, pos, HINT_NAV);
      dirtyLocal = false;
    }
    delay(5);
  }
}

bool askWifiOrManual() {
  const char *const LABELS[2] = {"Try WiFi Again", "Manual Mode (offline)"};
  int idx = 0;
  bool dirtyLocal = true;
  while (true) {
    bool lt, ll, rt, rl, ot, ol;
    btnLeft.poll(lt, ll);
    btnRight.poll(rt, rl);
    btnOk.poll(ot, ol);

    if (lt || rt) { idx = 1 - idx; dirtyLocal = true; }
    if (ot || ol) return idx == 1;

    if (dirtyLocal) {
      drawScreen("NO WIFI CONNECTION", COL_WARN, COL_BG, LABELS[idx], COL_SETTINGS_ACCENT,
                 "", "< > choose   OK confirm");
      dirtyLocal = false;
    }
    delay(5);
  }
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
          runSdBrowser(); // blocking; return value doesn't matter here
          dirty = true;   // redraw MAIN tiles once it's done
        } else if (mainIndex == 1) {
          state = ST_SETTINGS;
          settingsIndex = 0;
          dirty = true;
        } else {
          exitToClock(); // "Back" tile - same as holding OK
        }
      }
      break;
    }

    case ST_SETTINGS: {
      const int n = SETTINGS_COUNT;
      if (leftTap) { settingsIndex = (settingsIndex + n - 1) % n; dirty = true; }
      if (rightTap) { settingsIndex = (settingsIndex + 1) % n; dirty = true; }
      if (okLong) { state = ST_MAIN; dirty = true; }
      else if (okTap) {
        if (settingsIndex == 0) {
          runWifiPicker(); // blocking; return value doesn't matter here
          dirty = true;    // redraw the Settings list once it's done
        } else if (settingsIndex == 1) {
          findCurrentZone(continentIndex, zoneIndex);
          state = ST_CONTINENT;
          dirty = true;
        } else if (settingsIndex == 2) {
          runDateTimeSetter(); // blocking; redraws Settings once it's done
          dirty = true;
        } else {
          state = ST_ABOUT;
          dirty = true;
        }
      }
      break;
    }

    case ST_CONTINENT:
      if (leftTap) { continentIndex = (continentIndex + TZ_CONTINENT_COUNT - 1) % TZ_CONTINENT_COUNT; dirty = true; }
      if (rightTap) { continentIndex = (continentIndex + 1) % TZ_CONTINENT_COUNT; dirty = true; }
      if (okLong) { state = ST_SETTINGS; dirty = true; }
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

    case ST_ABOUT:
      if (okTap || okLong) { state = ST_SETTINGS; dirty = true; }
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
