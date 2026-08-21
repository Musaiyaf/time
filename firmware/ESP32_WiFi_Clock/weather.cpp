#include "weather.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <time.h>

namespace {

Preferences prefs;
const char *NVS_NAMESPACE = "clockcfg";

// ---- saved location ----------------------------------------------------
String savedCity;     // exactly what the user typed
String savedLabel;    // "City, Country" as the geocoder resolved it
float savedLat = 0, savedLon = 0;
bool cityKnown = false;

// ---- fetched forecast --------------------------------------------------
bool forecastValid = false;
float curTemp = 0, curFeels = 0, curWind = 0;
int curCode = 0, curHumidity = 0;

Weather::HourSlot hours[Weather::MAX_HOURS];
int hourN = 0;

unsigned long lastOkMs = 0;
bool everFetched = false;
bool fetching = false;
String lastError;

// Next fetch is due at this millis() value. Set to 0 so the very first
// loop() with a city and a connection fetches immediately.
unsigned long nextDueMs = 0;
const unsigned long REFRESH_OK_MS = 15UL * 60UL * 1000UL; // 15 min when it works
const unsigned long REFRESH_ERR_MS = 60UL * 1000UL;       // retry a failure sooner

// ---- tiny JSON helpers -------------------------------------------------
// Open-Meteo's responses are a fixed, flat shape (a couple of objects of
// scalars plus some arrays of numbers), so picking values out by key is a
// few lines of indexOf/atof - deliberately not worth pulling in a JSON
// library and its parse-time RAM for. Same reasoning as the key=value
// config parsing elsewhere in this firmware.
//
// Every lookup is scoped to a byte range so the "temperature_2m" inside
// "current" can't be confused with the one inside "hourly" (or with the
// units echo in "current_units"/"hourly_units"). Note that searching for
// the key *including* its quotes and colon - "temperature_2m": - is
// already unambiguous against "current_units": vs "current":, since they
// differ before the closing quote.

// Byte range of the object that follows "key":{ ... }. Returns false if
// the key isn't there. These objects contain no nested objects, so the
// next '}' really is the end of this one.
bool objectRange(const String &s, const char *keyWithQuotes, int &start, int &end) {
  int k = s.indexOf(keyWithQuotes);
  if (k < 0) return false;
  int brace = s.indexOf('{', k + (int)strlen(keyWithQuotes) - 1);
  if (brace < 0) return false;
  int close = s.indexOf('}', brace);
  if (close < 0) return false;
  start = brace;
  end = close;
  return true;
}

// Numeric value of "key": within [from, to). Returns fallback if absent.
float numberIn(const String &s, int from, int to, const char *keyWithQuotes, float fallback) {
  int k = s.indexOf(keyWithQuotes, from);
  if (k < 0 || k >= to) return fallback;
  int v = k + strlen(keyWithQuotes);
  while (v < to && (s[v] == ' ' || s[v] == ':')) v++;
  if (v >= to) return fallback;
  if (s.startsWith("null", v)) return fallback;
  return atof(s.c_str() + v);
}

// Reads the numeric array at "key":[ ... ] within [from, to) into out,
// stopping at maxOut values. A JSON null becomes nullValue. Returns how
// many were read.
int numberArrayIn(const String &s, int from, int to, const char *keyWithQuotes,
                   float *out, int maxOut, float nullValue) {
  int k = s.indexOf(keyWithQuotes, from);
  if (k < 0 || k >= to) return 0;
  int i = s.indexOf('[', k + (int)strlen(keyWithQuotes) - 1);
  if (i < 0 || i >= to) return 0;
  i++; // past '['
  int n = 0;
  while (i < to && n < maxOut) {
    while (i < to && (s[i] == ' ' || s[i] == ',')) i++;
    if (i >= to || s[i] == ']') break;
    if (s.startsWith("null", i)) {
      out[n++] = nullValue;
      i += 4;
    } else {
      out[n++] = atof(s.c_str() + i);
      while (i < to && s[i] != ',' && s[i] != ']') i++;
    }
  }
  return n;
}

// Reads the ISO-8601 timestamps at "time":[ "...", ... ] within
// [from, to), keeping only what we need to line them up against the
// system clock: a single comparable "when" key per entry, plus the local
// hour to label the column with. Returns how many were read.
//
// The key is ((year*12 + month)*31 + day)*24 + hour: not a real epoch,
// but strictly increasing across day/month/year boundaries, which is all
// the "which of these slots is now?" comparison below needs.
int timeArrayIn(const String &s, int from, int to, long *whenOut, int8_t *hourOut, int maxOut) {
  int k = s.indexOf("\"time\":", from);
  if (k < 0 || k >= to) return 0;
  int i = s.indexOf('[', k);
  if (i < 0 || i >= to) return 0;
  int n = 0;
  while (n < maxOut) {
    i = s.indexOf('"', i + 1);
    if (i < 0 || i >= to) break;
    int endq = s.indexOf('"', i + 1);
    if (endq < 0 || endq >= to) break;
    // Expect "YYYY-MM-DDTHH:MM" - anything shorter isn't a timestamp.
    // Read the digits straight out of the payload: building a String per
    // entry here would copy the whole remaining response (kilobytes)
    // once per hour parsed, for the sake of four small numbers.
    if (endq - i - 1 >= 16) {
      const char *t = s.c_str() + i + 1;
      int yr = (t[0] - '0') * 1000 + (t[1] - '0') * 100 + (t[2] - '0') * 10 + (t[3] - '0');
      int mo = (t[5] - '0') * 10 + (t[6] - '0');
      int dy = (t[8] - '0') * 10 + (t[9] - '0');
      int hr = (t[11] - '0') * 10 + (t[12] - '0');
      whenOut[n] = (((long)yr * 12 + mo) * 31 + dy) * 24 + hr;
      hourOut[n] = (int8_t)hr;
      n++;
    }
    i = endq;
    // Stop at the end of this array rather than running into the next one.
    int nextComma = s.indexOf(',', endq);
    int nextClose = s.indexOf(']', endq);
    if (nextClose >= 0 && (nextComma < 0 || nextClose < nextComma)) break;
  }
  return n;
}

// Text value of "key":"..." within [from, to).
String stringIn(const String &s, int from, int to, const char *keyWithQuotes) {
  int k = s.indexOf(keyWithQuotes, from);
  if (k < 0 || k >= to) return "";
  int q = s.indexOf('"', k + strlen(keyWithQuotes)); // opening quote of the value
  if (q < 0 || q >= to) return "";
  int endq = s.indexOf('"', q + 1);
  if (endq < 0 || endq >= to) return "";
  return s.substring(q + 1, endq);
}

// ---- HTTP --------------------------------------------------------------
String urlEncode(const String &in) {
  String out;
  out.reserve(in.length() * 3);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}

bool httpsGet(const String &url, String &out, String &errOut) {
  if (WiFi.status() != WL_CONNECTED) {
    errOut = "No WiFi";
    return false;
  }

  WiFiClientSecure client;
  // No certificate validation. This fetches public, read-only weather
  // data over a connection nothing sensitive goes out on, and pinning a
  // CA root here would mean re-flashing the clock whenever that root
  // rotates - a poor trade for a device with no other way to update it.
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(WEATHER_HTTP_TIMEOUT_MS);
  http.setTimeout(WEATHER_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    errOut = "Connect failed";
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    errOut = (code < 0) ? String("Network error") : ("HTTP " + String(code));
    http.end();
    return false;
  }
  out = http.getString();
  http.end();
  if (out.length() == 0) {
    errOut = "Empty reply";
    return false;
  }
  return true;
}

// ---- NVS ---------------------------------------------------------------
void saveLocation() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("wxcity", savedCity);
  prefs.putString("wxlabel", savedLabel);
  prefs.putFloat("wxlat", savedLat);
  prefs.putFloat("wxlon", savedLon);
  prefs.end();
}

} // namespace

namespace Weather {

void begin() {
  prefs.begin(NVS_NAMESPACE, true);
  savedCity = prefs.getString("wxcity", "");
  savedLabel = prefs.getString("wxlabel", "");
  savedLat = prefs.getFloat("wxlat", 0);
  savedLon = prefs.getFloat("wxlon", 0);
  prefs.end();
  cityKnown = savedCity.length() > 0;
}

String cityName() { return savedCity; }
String resolvedLabel() { return savedLabel.length() ? savedLabel : savedCity; }
bool hasCity() { return cityKnown; }
bool hasForecast() { return forecastValid; }

long secondsSinceUpdate() {
  if (!everFetched) return -1;
  return (long)((millis() - lastOkMs) / 1000UL);
}

String statusText() {
  if (fetching) return "Updating...";
  if (!cityKnown) return "No city set";
  if (WiFi.status() != WL_CONNECTED) return "No WiFi";
  if (!everFetched) return lastError.length() ? lastError : "Updating...";
  long age = secondsSinceUpdate();
  String when = (age < 90) ? "just now" : (String(age / 60) + " min ago");
  // A failure after a good fetch keeps showing the (stale) data, so say
  // so rather than silently presenting old numbers as current.
  if (lastError.length()) return lastError + " - showing " + when;
  return "Updated " + when;
}

float currentTempC() { return curTemp; }
float feelsLikeC() { return curFeels; }
int currentCode() { return curCode; }
int humidityPct() { return curHumidity; }
float windKph() { return curWind; }

int hourCount() { return hourN; }

const HourSlot &hourAt(int i) {
  static HourSlot empty = {0, 0, 0, -1};
  if (i < 0 || i >= hourN) return empty;
  return hours[i];
}

const char *codeText(int code) {
  // WMO 4677 weather codes, as documented by Open-Meteo.
  switch (code) {
    case 0:  return "Clear";
    case 1:  return "Mainly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: case 81: return "Rain showers";
    case 82: return "Heavy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunder + hail";
    default: return "Unknown";
  }
}

bool setCity(const String &name, String &errOut) {
  String trimmed = name;
  trimmed.trim();
  if (trimmed.length() == 0) {
    errOut = "Empty name";
    return false;
  }

  String url = String(WEATHER_GEOCODE_URL) + "?name=" + urlEncode(trimmed) +
               "&count=1&language=en&format=json";
  String body;
  fetching = true;
  bool ok = httpsGet(url, body, errOut);
  fetching = false;
  if (!ok) return false;

  // No match at all: the response is just {"generationtime_ms":...} with
  // no "results" array.
  int r = body.indexOf("\"results\"");
  if (r < 0) {
    errOut = "City not found";
    return false;
  }

  int end = body.length();
  float lat = numberIn(body, r, end, "\"latitude\":", 1e9f);
  float lon = numberIn(body, r, end, "\"longitude\":", 1e9f);
  if (lat > 1e8f || lon > 1e8f) {
    errOut = "City not found";
    return false;
  }

  String resolved = stringIn(body, r, end, "\"name\":");
  String country = stringIn(body, r, end, "\"country\":");

  savedCity = trimmed;
  savedLabel = resolved.length() ? resolved : trimmed;
  if (country.length()) savedLabel += ", " + country;
  savedLat = lat;
  savedLon = lon;
  cityKnown = true;
  saveLocation();

  // The old forecast is for somewhere else now.
  forecastValid = false;
  everFetched = false;
  hourN = 0;
  lastError = "";
  nextDueMs = millis(); // fetch for the new location on the next loop()
  return true;
}

void clearCity() {
  savedCity = "";
  savedLabel = "";
  savedLat = savedLon = 0;
  cityKnown = false;
  forecastValid = false;
  everFetched = false;
  hourN = 0;
  lastError = "";
  saveLocation();
}

bool refreshNow() {
  if (!cityKnown) {
    lastError = "No city set";
    return false;
  }

  char url[320];
  snprintf(url, sizeof(url),
           "%s?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m"
           "&hourly=temperature_2m,weather_code,precipitation_probability"
           "&forecast_hours=%d&timezone=auto",
           WEATHER_FORECAST_URL, savedLat, savedLon, WEATHER_FORECAST_HOURS);

  String body, err;
  fetching = true;
  bool ok = httpsGet(String(url), body, err);
  fetching = false;
  if (!ok) {
    lastError = err;
    nextDueMs = millis() + REFRESH_ERR_MS;
    return false;
  }

  int cs, ce;
  if (!objectRange(body, "\"current\":", cs, ce)) {
    lastError = "Bad reply";
    nextDueMs = millis() + REFRESH_ERR_MS;
    return false;
  }
  float t = numberIn(body, cs, ce, "\"temperature_2m\":", 1e9f);
  if (t > 1e8f) {
    lastError = "Bad reply";
    nextDueMs = millis() + REFRESH_ERR_MS;
    return false;
  }
  curTemp = t;
  curHumidity = (int)numberIn(body, cs, ce, "\"relative_humidity_2m\":", 0);
  curFeels = numberIn(body, cs, ce, "\"apparent_temperature\":", t);
  curCode = (int)numberIn(body, cs, ce, "\"weather_code\":", 0);
  curWind = numberIn(body, cs, ce, "\"wind_speed_10m\":", 0);

  hourN = 0;
  int hs, he;
  if (objectRange(body, "\"hourly\":", hs, he)) {
    static float temps[MAX_HOURS], codes[MAX_HOURS], precip[MAX_HOURS];
    static long whens[MAX_HOURS];
    static int8_t hrs[MAX_HOURS];

    int nT = numberArrayIn(body, hs, he, "\"temperature_2m\":", temps, MAX_HOURS, 0);
    int nC = numberArrayIn(body, hs, he, "\"weather_code\":", codes, MAX_HOURS, 0);
    int nP = numberArrayIn(body, hs, he, "\"precipitation_probability\":", precip, MAX_HOURS, -1);
    int nH = timeArrayIn(body, hs, he, whens, hrs, MAX_HOURS);

    int n = nT;
    if (nH < n) n = nH;

    // Line the slots up against the wall clock. Asking for
    // forecast_hours already makes slot 0 the current hour, but not
    // depending on that means a response starting at midnight (what
    // forecast_days returns) still shows the *next* hours rather than
    // this morning's.
    int start = 0;
    struct tm now;
    if (getLocalTime(&now, 5)) {
      long nowKey = (((long)(now.tm_year + 1900) * 12 + (now.tm_mon + 1)) * 31 + now.tm_mday) * 24 +
                    now.tm_hour;
      for (int i = 0; i < n; i++) {
        if (whens[i] >= nowKey) {
          start = i;
          break;
        }
      }
    }

    for (int i = start; i < n; i++) {
      HourSlot &slot = hours[hourN];
      slot.hour = hrs[i];
      slot.tempC = temps[i];
      slot.code = (i < nC) ? (int16_t)codes[i] : 0;
      slot.precipPct = (i < nP) ? (int8_t)precip[i] : -1;
      hourN++;
    }
  }

  forecastValid = true;
  everFetched = true;
  lastError = "";
  lastOkMs = millis();
  nextDueMs = lastOkMs + REFRESH_OK_MS;
  return true;
}

void loop() {
  if (!cityKnown) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if ((long)(millis() - nextDueMs) < 0) return;
  refreshNow();
}

} // namespace Weather
