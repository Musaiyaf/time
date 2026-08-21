#pragma once
#include <Arduino.h>

// Current conditions + a next-few-hours forecast for one saved city,
// fetched from Open-Meteo (https://open-meteo.com) - chosen because its
// forecast and geocoding endpoints need no API key and no account, so
// there's nothing extra for a user to sign up for or paste into the
// setup page beyond the city name itself.
//
// The city is stored in NVS alongside the WiFi/time settings, as a name
// plus the latitude/longitude it resolved to, so a reboot doesn't need
// to geocode again. Set it from the on-device Settings menu or the web
// portal; view the forecast by holding LEFT on any clock face.
//
// Entirely optional: with no city set (or no WiFi) the weather screen
// just says so, and nothing else in the firmware is affected.
namespace Weather {

// One hourly forecast slot.
struct HourSlot {
  int8_t hour;      // local hour of day, 0-23
  float tempC;
  int16_t code;     // WMO weather code - see codeText()/codeIcon()
  int8_t precipPct; // chance of precipitation, 0-100 (-1 if unknown)
};

// Most hours we keep. The fetch asks for 12; the rest is headroom in
// case the API ever returns a longer hourly block than requested.
const int MAX_HOURS = 24;

// Loads the saved city/coordinates from NVS. Call once from setup(),
// after WifiManager::begin().
void begin();

// The saved city as the user typed it ("" if none is set), and the
// fuller "City, Country" label the geocoder resolved it to.
String cityName();
String resolvedLabel();
bool hasCity();

// The two-letter country code the city resolved to ("LK", "GB", ...),
// or "" if no city is set. The holiday calendar (calendar_events.h)
// looks up its country from here rather than asking separately.
String countryCode();

// Geocodes name and, if it resolves, saves it as the active city and
// clears any previously fetched forecast (so the next refresh fetches
// for the new location). Blocking - it makes one HTTPS request. Returns
// false and fills errOut with a short, displayable reason on failure.
// Requires an active WiFi connection.
bool setCity(const String &name, String &errOut);

// Forgets the saved city and any fetched forecast.
void clearCity();

// True once a forecast has been fetched successfully and is being kept
// (it stays true while a later refresh fails, so a temporary network
// blip shows slightly stale data rather than an empty screen).
bool hasForecast();

// Seconds since the forecast was last successfully fetched, or -1 if it
// never has been this session.
long secondsSinceUpdate();

// Short human-readable state for the weather screen's status line -
// "No city set", "No WiFi", "Updating...", "Updated 4 min ago", or the
// last error.
String statusText();

// Current conditions (only meaningful when hasForecast()).
float currentTempC();
float feelsLikeC();
int currentCode();
int humidityPct();
float windKph();

// The hourly forecast, oldest (nearest to now) first.
int hourCount();
const HourSlot &hourAt(int i);

// Short description for a WMO weather code ("Partly cloudy", "Rain", ...).
const char *codeText(int code);

// Fetches a fresh forecast right now, blocking. Returns false if there's
// no city, no WiFi, or the request failed. Normally you don't need to
// call this - loop() handles refreshing on its own schedule.
bool refreshNow();

// Call once per loop() iteration. Fetches a forecast when one is due
// (roughly every 15 minutes, or sooner after a failure) and there's a
// city and a WiFi connection; does nothing otherwise. Each fetch briefly
// blocks the main loop, so this deliberately never runs while a menu or
// the weather screen is on-screen - see menu.cpp.
void loop();

} // namespace Weather
