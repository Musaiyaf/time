#pragma once
#include <Arduino.h>

// Public holidays and festivals for the clock's country, fetched from
// Nager.Date (https://date.nager.at) - like the weather API it needs no
// key and no account.
//
// Which country to ask about comes from the weather city's resolved
// country code (see Weather::countryCode()), so setting a city is the
// only configuration either feature needs. Nager.Date doesn't cover
// every country; when it doesn't have the clock's, the calendar screen
// says so rather than sitting empty.
//
// Hold RIGHT on any clock face to open the calendar.
namespace CalendarEvents {

struct Event {
  int16_t year;
  int8_t month; // 1-12
  int8_t day;   // 1-31
  char name[40];
};

// Loads nothing from storage - holidays are re-fetched each boot, since
// the request is small and the data only changes once a year. Call once
// from setup().
void begin();

// True once a holiday list has been fetched successfully.
bool hasData();

// Short human-readable state for the calendar screen - "No city set",
// "No WiFi", "Updating...", "Not available for XX", or an error.
String statusText();

// All known events, earliest first.
int count();
const Event &at(int i);

// Index of the first event on or after the given date, or count() if
// they're all in the past.
int firstOnOrAfter(int year, int month, int day);

// True if any event falls on this date - used to mark days in the
// month grid.
bool isEventDay(int year, int month, int day);

// The name of the event on this date, or "" if there isn't one.
const char *nameOn(int year, int month, int day);

// Fetches the holiday list right now, blocking. Returns false if there's
// no country known, no WiFi, or the request failed.
bool refreshNow();

// Call once per loop() iteration. Fetches when due (once a day, or
// sooner after a failure) and there's a country and a connection.
void loop();

} // namespace CalendarEvents
