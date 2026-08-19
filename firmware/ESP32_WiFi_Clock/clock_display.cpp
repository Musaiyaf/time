#include "clock_display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <Fonts/GFXFF/FreeSansBold9pt7b.h>
#include <Fonts/GFXFF/FreeSansBold12pt7b.h>

namespace {

TFT_eSPI tft = TFT_eSPI();

// Reusable offscreen buffers, sized exactly to what they draw and created
// once at startup (never deleted/recreated) so a clock left running for
// weeks doesn't churn the heap. Redrawn only when their content actually
// changes, so the display never has to flicker-clear the whole panel.
TFT_eSprite digitSpr(&tft);
TFT_eSprite dateSpr(&tft);
TFT_eSprite weekSpr(&tft);
TFT_eSprite doySpr(&tft);
TFT_eSprite wifiSpr(&tft);

// ---- Theme colours (approximating the reference photo) -------------
const uint16_t COL_BG        = TFT_BLACK;
const uint16_t COL_GRID      = tft.color565(55, 60, 68);
const uint16_t COL_DIGIT     = TFT_WHITE;
const uint16_t COL_DATE_BG   = tft.color565(27, 111, 209);
const uint16_t COL_WEEK_BG   = tft.color565(46, 163, 89);
const uint16_t COL_DOY_BG    = tft.color565(224, 133, 45);
const uint16_t COL_WIFI_BG   = tft.color565(27, 168, 163);
const uint16_t COL_WIFI_BAD  = tft.color565(150, 40, 40);
const uint16_t COL_ACCENT    = tft.color565(255, 90, 90);
const uint16_t COL_BADGE_TXT = TFT_WHITE;

// ---- Layout -----------------------------------------------------------
const int SCR_W = TFT_SCREEN_WIDTH;
const int SCR_H = TFT_SCREEN_HEIGHT;

const int TOPBAR_H = 30;

struct Badge { int x, w; uint16_t color; };
const Badge B_DATE = {0, 104, COL_DATE_BG};
const Badge B_WEEK = {104, 86, COL_WEEK_BG};
const Badge B_DOY  = {190, 96, COL_DOY_BG};
const Badge B_WIFI = {286, 34, COL_WIFI_BG};

const int CLOCK_TOP = TOPBAR_H;
const int CLOCK_H   = SCR_H - TOPBAR_H;
const int CELL_COUNT = 8; // HH:MM:SS -> 2 digits, colon, 2 digits, colon, 2 digits
const int CELL_W = SCR_W / CELL_COUNT; // 40px

int digitTextSize = 1;

// ---- state cache, so we only repaint what changed --------------------
char lastDigit[CELL_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
bool gridDrawn = false;
String lastDateStr = "\x01";
String lastWeekStr = "\x01";
int lastYday = -999;
bool lastWifiConnected = true; // force first draw
int lastWifiBars = -1;

bool isColonCell(int col) { return col == 2 || col == 5; }

void drawGrid() {
  tft.fillRect(0, CLOCK_TOP, SCR_W, CLOCK_H, COL_BG);
  for (int i = 0; i <= CELL_COUNT; i++) {
    int x = i * CELL_W;
    // dashed vertical separator
    for (int y = CLOCK_TOP + 4; y < SCR_H - 4; y += 6) {
      tft.drawFastVLine(x == SCR_W ? x - 1 : x, y, 3, COL_GRID);
    }
    // small tick marks top & bottom, like grid intersections
    int xx = (x == SCR_W) ? x - 2 : x;
    tft.fillRect(xx, CLOCK_TOP, 2, 2, COL_GRID);
    tft.fillRect(xx, SCR_H - 2, 2, 2, COL_GRID);
  }
}

void computeDigitTextSize() {
  tft.setTextFont(7);
  digitTextSize = 1;
  for (int s = 1; s <= 4; s++) {
    tft.setTextSize(s);
    int w = tft.textWidth("0");
    int h = tft.fontHeight();
    if (w <= CELL_W - 8 && h <= CLOCK_H - 14) {
      digitTextSize = s;
    } else {
      break;
    }
  }
  tft.setTextSize(1);
}

void drawDigitCell(int col, char ch) {
  int x = col * CELL_W;
  digitSpr.fillSprite(COL_BG);
  digitSpr.setTextFont(7);
  digitSpr.setTextSize(digitTextSize);
  digitSpr.setTextColor(COL_DIGIT, COL_BG);
  digitSpr.setTextDatum(MC_DATUM);
  digitSpr.drawString(String(ch), CELL_W / 2, CLOCK_H / 2);
  digitSpr.pushSprite(x, CLOCK_TOP);
}

void drawColonCell(int col) {
  int x = col * CELL_W;
  digitSpr.fillSprite(COL_BG);
  int cx = CELL_W / 2;
  int cy = CLOCK_H / 2;
  int r = max(3, CELL_W / 10);
  int gap = CLOCK_H / 6;
  digitSpr.fillSmoothCircle(cx, cy - gap, r, COL_DIGIT, COL_BG);
  digitSpr.fillSmoothCircle(cx, cy + gap, r, COL_DIGIT, COL_BG);
  digitSpr.pushSprite(x, CLOCK_TOP);
}

void centerText2(TFT_eSprite &spr, const Badge &b, const String &part1, uint16_t col1,
                  const String &part2, uint16_t col2) {
  spr.fillSprite(b.color);
  spr.setFreeFont(&FreeSansBold9pt7b);
  int w1 = part1.length() ? spr.textWidth(part1) : 0;
  int w2 = part2.length() ? spr.textWidth(part2) : 0;
  int total = w1 + w2;
  int startX = (b.w - total) / 2;
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

void drawWifiBadge(bool connected, int rssi) {
  const Badge &b = B_WIFI;
  uint16_t bg = connected ? b.color : COL_WIFI_BAD;
  wifiSpr.fillSprite(bg);

  int bars = 0;
  if (connected) {
    if (rssi > -55) bars = 4;
    else if (rssi > -65) bars = 3;
    else if (rssi > -75) bars = 2;
    else bars = 1;
  }

  int baseX = b.w / 2 - 10;
  int baseY = TOPBAR_H - 7;
  for (int i = 0; i < 4; i++) {
    int barH = 4 + i * 3;
    int bx = baseX + i * 5;
    if (i < bars) {
      wifiSpr.fillRect(bx, baseY - barH, 3, barH, TFT_WHITE);
    } else {
      wifiSpr.drawRect(bx, baseY - barH, 3, barH, tft.color565(230, 230, 230));
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
  dateSpr.setColorDepth(16);
  weekSpr.setColorDepth(16);
  doySpr.setColorDepth(16);
  wifiSpr.setColorDepth(16);

  digitSpr.createSprite(CELL_W, CLOCK_H);
  dateSpr.createSprite(B_DATE.w, TOPBAR_H);
  weekSpr.createSprite(B_WEEK.w, TOPBAR_H);
  doySpr.createSprite(B_DOY.w, TOPBAR_H);
  wifiSpr.createSprite(B_WIFI.w, TOPBAR_H);

  computeDigitTextSize();
  drawGrid();
  gridDrawn = true;
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
  tft.fillScreen(COL_DATE_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(TFT_WHITE, COL_DATE_BG);
  tft.drawString("WiFi Setup", SCR_W / 2, 34);

  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.drawString("Connect your phone to:", SCR_W / 2, 68);
  tft.setTextColor(TFT_YELLOW, COL_DATE_BG);
  tft.drawString(apName, SCR_W / 2, 92);
  tft.setTextColor(TFT_WHITE, COL_DATE_BG);
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
    lastWeekStr = "\x01";
    lastYday = -999;
    lastWifiConnected = !wifiConnected; // force redraw
    lastWifiBars = -999;
  }

  // ---- clock digits ----
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d%02d%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  // buf: H H M M S S  -> map into the 8 cells (2 colon cells in between)
  const char *src = buf;
  int srcIdx = 0;
  for (int col = 0; col < CELL_COUNT; col++) {
    if (isColonCell(col)) {
      if (lastDigit[col] != ':') {
        drawColonCell(col);
        lastDigit[col] = ':';
      }
      continue;
    }
    char ch = src[srcIdx++];
    if (lastDigit[col] != ch) {
      drawDigitCell(col, ch);
      lastDigit[col] = ch;
    }
  }

  // ---- date badge (YYYY-MM-DD, day-of-month in accent colour) ----
  char dateBuf[11];
  snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", timeinfo.tm_year + 1900,
           timeinfo.tm_mon + 1, timeinfo.tm_mday);
  String dateStr(dateBuf);
  if (dateStr != lastDateStr) {
    lastDateStr = dateStr;
    String prefix = dateStr.substring(0, 8); // "YYYY-MM-"
    String day = dateStr.substring(8);       // "DD"
    centerText2(dateSpr, B_DATE, prefix, COL_BADGE_TXT, day, COL_ACCENT);
  }

  // ---- weekday badge ----
  static const char *WD[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  String weekStr = WD[timeinfo.tm_wday];
  if (weekStr != lastWeekStr) {
    lastWeekStr = weekStr;
    centerText2(weekSpr, B_WEEK, weekStr, COL_BADGE_TXT, "", COL_BADGE_TXT);
  }

  // ---- day-of-year badge ----
  if (timeinfo.tm_yday != lastYday) {
    lastYday = timeinfo.tm_yday;
    char doyBuf[10];
    snprintf(doyBuf, sizeof(doyBuf), "DAY %03d", timeinfo.tm_yday + 1);
    centerText2(doySpr, B_DOY, String(doyBuf), COL_BADGE_TXT, "", COL_BADGE_TXT);
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
