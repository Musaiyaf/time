#include "calendar_events.h"
#include "config.h"
#include "net_fetch.h"
#include "weather.h"
#include <WiFi.h>
#include <time.h>

namespace {

CalendarEvents::Event events[HOLIDAY_MAX_EVENTS];
int eventN = 0;

bool everFetched = false;
bool fetching = false;
String lastError;
String fetchedForCC;   // country the current list belongs to

unsigned long nextDueMs = 0;
const unsigned long REFRESH_OK_MS = 12UL * 60UL * 60UL * 1000UL; // twice a day
const unsigned long REFRESH_ERR_MS = 5UL * 60UL * 1000UL;

// Copies a holiday name into the fixed-size slot, dropping anything
// outside printable ASCII. The display fonts are ASCII-only (they're
// Adafruit GFX free fonts), so an accented or non-Latin character would
// otherwise come out as a stray glyph rather than the letter meant.
void copyName(char *dest, size_t destSize, const String &src) {
  size_t o = 0;
  for (size_t i = 0; i < src.length() && o + 1 < destSize; i++) {
    char c = src[i];
    if (c >= 32 && c <= 126) dest[o++] = c;
  }
  // Trim a trailing space left behind by a dropped character.
  while (o > 0 && dest[o - 1] == ' ') o--;
  dest[o] = '\0';
}

// Nager.Date returns a flat array of objects, each starting
// {"date":"YYYY-MM-DD","localName":"...","name":"...",...}. Walking it
// by "date" and then taking the "name" that follows is enough - no need
// to track object boundaries, since the fields always come in that
// order. "name" is the English name; "localName" would often be in a
// script the display can't render.
//
// Appends to whatever's already collected, so the current year and next
// year can both be pulled in.
int parseInto(const String &body, int startIndex) {
  int n = startIndex;
  int i = 0;
  while (n < HOLIDAY_MAX_EVENTS) {
    int d = body.indexOf("\"date\":\"", i);
    if (d < 0) break;
    int v = d + 8;
    if (v + 10 > (int)body.length()) break;

    CalendarEvents::Event &e = events[n];
    e.year = (body[v] - '0') * 1000 + (body[v + 1] - '0') * 100 +
             (body[v + 2] - '0') * 10 + (body[v + 3] - '0');
    e.month = (body[v + 5] - '0') * 10 + (body[v + 6] - '0');
    e.day = (body[v + 8] - '0') * 10 + (body[v + 9] - '0');

    int nm = body.indexOf("\"name\":\"", v);
    if (nm < 0) break;
    int nq = body.indexOf('"', nm + 8);
    copyName(e.name, sizeof(e.name), body.substring(nm + 8, nq < 0 ? nm + 8 : nq));

    if (e.month >= 1 && e.month <= 12 && e.day >= 1 && e.day <= 31 && e.name[0]) n++;
    i = (nq > 0) ? nq : v + 10;
  }
  return n;
}

long dateKey(int y, int m, int d) { return ((long)y * 12 + m) * 31 + d; }

bool today(int &y, int &m, int &d) {
  struct tm t;
  if (!getLocalTime(&t, 5)) return false;
  y = t.tm_year + 1900;
  m = t.tm_mon + 1;
  d = t.tm_mday;
  return true;
}

} // namespace

namespace CalendarEvents {

void begin() {
  eventN = 0;
  everFetched = false;
  nextDueMs = 0;
}

bool hasData() { return everFetched && eventN > 0; }
int count() { return eventN; }

const Event &at(int i) {
  static Event empty = {0, 0, 0, ""};
  if (i < 0 || i >= eventN) return empty;
  return events[i];
}

String statusText() {
  if (fetching) return "Updating...";
  String cc = Weather::countryCode();
  if (cc.length() == 0) return "No city set";
  if (!everFetched && WiFi.status() != WL_CONNECTED) return "No WiFi";
  if (everFetched && eventN == 0) return "Not available for " + cc;
  if (!everFetched) return lastError.length() ? lastError : "Updating...";
  if (lastError.length()) return lastError;
  return cc;
}

int firstOnOrAfter(int year, int month, int day) {
  long key = dateKey(year, month, day);
  for (int i = 0; i < eventN; i++) {
    if (dateKey(events[i].year, events[i].month, events[i].day) >= key) return i;
  }
  return eventN;
}

bool isEventDay(int year, int month, int day) {
  for (int i = 0; i < eventN; i++) {
    if (events[i].year == year && events[i].month == month && events[i].day == day) return true;
  }
  return false;
}

const char *nameOn(int year, int month, int day) {
  for (int i = 0; i < eventN; i++) {
    if (events[i].year == year && events[i].month == month && events[i].day == day) {
      return events[i].name;
    }
  }
  return "";
}

bool refreshNow() {
  String cc = Weather::countryCode();
  if (cc.length() != 2) {
    lastError = "No city set";
    return false;
  }

  int y, m, d;
  if (!today(y, m, d)) {
    lastError = "No clock time";
    nextDueMs = millis() + REFRESH_ERR_MS;
    return false;
  }

  String body, err;
  fetching = true;
  bool ok = NetFetch::get(String(HOLIDAY_API_URL) + "/" + String(y) + "/" + cc, body, err);
  fetching = false;

  if (!ok) {
    // A 404 here means Nager.Date simply has no data for this country,
    // which is a permanent answer rather than a transient failure -
    // record it as an empty (but successful) list so the screen can say
    // so instead of retrying every five minutes forever.
    if (err == "HTTP 404") {
      eventN = 0;
      everFetched = true;
      fetchedForCC = cc;
      lastError = "";
      nextDueMs = millis() + REFRESH_OK_MS;
      return true;
    }
    lastError = err;
    nextDueMs = millis() + REFRESH_ERR_MS;
    return false;
  }

  eventN = parseInto(body, 0);

  // Late in the year most of this year's holidays are behind us, so pull
  // next year's too rather than showing an "upcoming" list that's nearly
  // empty every December.
  if (eventN < HOLIDAY_MAX_EVENTS && firstOnOrAfter(y, m, d) > eventN - 4) {
    String body2, err2;
    if (NetFetch::get(String(HOLIDAY_API_URL) + "/" + String(y + 1) + "/" + cc, body2, err2)) {
      eventN = parseInto(body2, eventN);
    }
  }

  everFetched = true;
  fetchedForCC = cc;
  lastError = "";
  nextDueMs = millis() + REFRESH_OK_MS;
  return true;
}

void loop() {
  String cc = Weather::countryCode();
  if (cc.length() != 2) return;
  if (WiFi.status() != WL_CONNECTED) return;
  // A new city in a different country invalidates the list immediately,
  // whatever the refresh schedule said.
  if (everFetched && cc != fetchedForCC) {
    everFetched = false;
    eventN = 0;
    nextDueMs = millis();
  }
  if ((long)(millis() - nextDueMs) < 0) return;
  refreshNow();
}

} // namespace CalendarEvents
