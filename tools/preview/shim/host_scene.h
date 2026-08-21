#pragma once
// The canned state a preview screen is rendered from. One global that
// each scene poses before drawing, so the layout can be pushed to its
// edges on purpose - a very long city name, 100% humidity, a festival
// falling on today - rather than only ever being seen with whatever
// values the real world happened to supply.

#include <Arduino.h>

#include "config.h" // HOLIDAY_MAX_EVENTS
#include "calendar_events.h"
#include "weather.h"

struct HostScene {
  // Weather
  String city = "Kattankudy";
  String cityLabel = "Kattankudy, Sri Lanka";
  String countryCode = "LK";
  bool online = true;
  bool hasForecast = true;
  long forecastAgeSecs = 30;
  float tempC = 33;
  float feelsC = 38;
  int code = 51;
  int humidity = 59;
  float windKph = 12;

  Weather::HourSlot hours[Weather::MAX_HOURS] = {};
  int hourCount = 0;

  // Calendar
  CalendarEvents::Event events[HOLIDAY_MAX_EVENTS] = {};
  int eventCount = 0;
  String calendarStatus; // "" means "use the country code"

  void addHour(int hour, float t, int wcode, int precip) {
    if (hourCount >= Weather::MAX_HOURS) return;
    hours[hourCount++] = {(int8_t)hour, t, (int16_t)wcode, (int8_t)precip};
  }
  void addEvent(int y, int m, int d, const char *name) {
    if (eventCount >= HOLIDAY_MAX_EVENTS) return;
    CalendarEvents::Event &e = events[eventCount++];
    e.year = (int16_t)y;
    e.month = (int8_t)m;
    e.day = (int8_t)d;
    // Deliberately truncating: a name longer than the real firmware's
    // Event::name[40] is exactly the case this fixture exists to pose.
    snprintf(e.name, sizeof(e.name), "%.39s", name);
  }
};

extern HostScene scene;
