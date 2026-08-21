// Stand-ins for the firmware modules that talk to hardware or the
// network. The point of the harness is to exercise the real *drawing*
// code, so everything it draws *from* is canned here - and canned data
// is better than live data anyway, since it can be posed deliberately
// (long city names, 100% humidity, a festival on today) to test the
// layout's edges instead of whatever the weather happens to be.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>

#include "calendar_events.h"
#include "clock_display.h"
#include "sd_card.h"
#include "weather.h"
#include "wifi_manager.h"
#include "host_scene.h"

WiFiStub WiFi;
HostScene scene;

// ---- the one shared display ---------------------------------------------
static TFT_eSPI hostTft(320, 170);

namespace ClockDisplay {
TFT_eSPI &rawDisplay() { return hostTft; }
void forceFullRedraw() {}
void nextFace() {}
void prevFace() {}
void begin() {}
void showBootMessage(const String &, const String &) {}
void showSetupScreen(const String &, const String &) {}
void update(const struct tm &, bool, bool, int) {}
} // namespace ClockDisplay

// ---- weather ------------------------------------------------------------
namespace Weather {

void begin() {}
String cityName() { return scene.city; }
String resolvedLabel() { return scene.cityLabel; }
bool hasCity() { return scene.cityLabel.length() > 0; }
String countryCode() { return scene.countryCode; }
bool ensureCountryCode() { return scene.countryCode.length() == 2; }
bool hasForecast() { return scene.hasForecast; }
long secondsSinceUpdate() { return scene.forecastAgeSecs; }

String statusText() {
  if (!hasCity()) return "No city set";
  if (!scene.online) return "No WiFi";
  if (!scene.hasForecast) return "Updating...";
  long age = scene.forecastAgeSecs;
  return "Updated " + String((age < 90) ? "just now" : (String(age / 60) + " min ago"));
}

float currentTempC() { return scene.tempC; }
float feelsLikeC() { return scene.feelsC; }
int currentCode() { return scene.code; }
int humidityPct() { return scene.humidity; }
float windKph() { return scene.windKph; }
int hourCount() { return scene.hourCount; }

const HourSlot &hourAt(int i) {
  static HourSlot empty = {0, 0, 0, -1};
  if (i < 0 || i >= scene.hourCount) return empty;
  return scene.hours[i];
}

const char *codeText(int code) {
  switch (code) {
    case 0: return "Clear";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 80: case 81: return "Rain showers";
    case 82: return "Heavy showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunder + hail";
    default: return "Unknown";
  }
}

bool setCity(const String &, String &) { return true; }
void clearCity() {}
bool refreshNow() { return scene.hasForecast; }
void loop() {}

} // namespace Weather

// ---- calendar -----------------------------------------------------------
namespace CalendarEvents {

void begin() {}
bool hasData() { return scene.eventCount > 0; }
int count() { return scene.eventCount; }

const Event &at(int i) {
  static Event empty = {0, 0, 0, ""};
  if (i < 0 || i >= scene.eventCount) return empty;
  return scene.events[i];
}

String statusText() {
  if (scene.calendarStatus.length()) return scene.calendarStatus;
  return scene.countryCode;
}

int firstOnOrAfter(int year, int month, int day) {
  long key = ((long)year * 12 + month) * 31 + day;
  for (int i = 0; i < scene.eventCount; i++) {
    const Event &e = scene.events[i];
    if (((long)e.year * 12 + e.month) * 31 + e.day >= key) return i;
  }
  return scene.eventCount;
}

bool isEventDay(int year, int month, int day) {
  for (int i = 0; i < scene.eventCount; i++) {
    const Event &e = scene.events[i];
    if (e.year == year && e.month == month && e.day == day) return true;
  }
  return false;
}

const char *nameOn(int year, int month, int day) {
  for (int i = 0; i < scene.eventCount; i++) {
    const Event &e = scene.events[i];
    if (e.year == year && e.month == month && e.day == day) return e.name;
  }
  return "";
}

bool refreshNow() { return hasData(); }
void loop() {}

} // namespace CalendarEvents

// ---- hardware modules the menus reference but the preview never uses ----
namespace WifiManager {
void begin() {}
bool loadCredentials(String &, String &) { return false; }
void saveCredentials(const String &, const String &) {}
void clearCredentials() {}
void loadTimeConfig(String &tz, String &n1, String &n2) { tz = "UTC0"; n1 = ""; n2 = ""; }
void saveTimeConfig(const String &, const String &, const String &) {}
bool connectSTA(const String &, const String &, unsigned long) { return false; }
void startMDNS() {}
void startAP() {}
void stopAP() {}
String getAPName() { return String("ESP32-Clock-Setup"); }
String getAPIP() { return String("192.168.4.1"); }
void syncTime() {}
void enterManualMode() {}
void setManualDateTime(int, int, int, int, int) {}
void backupTimeToRtc() {}
} // namespace WifiManager

namespace SdCard {
void begin() {}
bool isPresent() { return false; }
int listDir(const String &, Entry *, int) { return 0; }
size_t readAt(const String &, size_t, uint8_t *, size_t) { return 0; }
bool openSeqRead(const String &) { return false; }
size_t readSeqAt(size_t, uint8_t *, size_t) { return 0; }
void closeSeqRead() {}
bool exists(const String &) { return false; }
bool remove(const String &) { return true; }
bool beginWrite(const String &) { return false; }
bool writeChunk(const uint8_t *, size_t) { return false; }
void endWrite() {}
} // namespace SdCard
