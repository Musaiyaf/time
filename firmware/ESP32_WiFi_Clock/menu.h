#pragma once
#include <Arduino.h>

// On-device settings menu, driven by three buttons (config.h BTN_LEFT_PIN /
// BTN_RIGHT_PIN / BTN_OK_PIN):
//   - On a clock face: LEFT/RIGHT cycle faces, holding LEFT opens the
//     weather screen (see weather.h), holding OK opens this menu.
//   - Inside the menu: LEFT/RIGHT move the selection, a short tap of OK
//     confirms it, holding OK goes back a level (or exits to the clock
//     face from the top level).
// Menu contents: Settings (WiFi, Time Zone, Date/Time, Weather City,
// Alarm, Button Sound, About - a scrollable list) and Back. WiFi scans
// for nearby networks, lets you pick one and type its password on an
// on-screen keyboard, then connects. Time Zone picks a continent, then a
// specific zone within it - covers the same ~430 IANA zones as the web
// setup page. Date/Time sets the clock by hand, field by field - used
// for Manual (offline) mode, or just to correct the time. Weather City
// types a city name on the same on-screen keyboard and looks it up.
// Alarm sets a single daily on/off time, field by field like Date/Time -
// see alarm.h for how it's checked and rung. About shows the device's
// current IP/hostname.
namespace Menu {

void begin();

// Call once per loop() iteration, however often that runs (not throttled
// like ClockDisplay::update()) so button presses feel responsive. Returns
// true while a menu screen owns the display - the caller should skip its
// normal clock rendering that iteration in that case.
bool handle();

// Blocking: scans for WiFi networks, lets the user pick one, enter its
// password if needed, and connects - drawing its own screens the whole
// time. Returns true once connected (credentials are saved to NVS before
// returning), false if the user cancelled or every attempt failed. Used
// both from the Settings menu and from setup() as a fallback when
// auto-connecting to the saved network fails.
bool runWifiPicker();

// Blocking: shows a two-way choice ("Try WiFi Again" / "Manual Mode
// (offline)") and returns once OK confirms one. Returns true for Manual
// Mode, false for "try again" (the caller should then re-run
// runWifiPicker()). Used from setup() when there's no WiFi to connect to.
bool askWifiOrManual();

// Blocking: shows a fullscreen "ALARM" message and beeps the buzzer
// (independent of the Button Sound setting - see buzzer.h) until any
// button is pressed. Called from ESP32_WiFi_Clock.ino's loop() when
// Alarm::checkDue() fires.
void runAlarmRingingScreen();

#ifdef HOST_PREVIEW
// Host-preview only (see tools/preview): thin pass-throughs to the
// otherwise-file-local drawWeatherScreen()/drawCalendarScreen(), so the
// desktop render harness can paint the exact same screens the device
// does without duplicating their layout code. Not compiled into the
// firmware - guarded by the same macro the harness's build defines.
void previewWeatherScreen(int scroll);
void previewCalendarScreen(int viewYear, int viewMonth);
// These two mirror runWeatherScreen()'s own gating exactly (same
// strings, same call), rather than just calling previewWeatherScreen()
// on posed empty data - on the device, "no city"/"no forecast yet"
// never reach drawWeatherScreen() at all, they replace the whole screen
// with drawWeatherMessage() instead. A preview scene that called
// previewWeatherScreen() directly for these states would be showing a
// composite (full current-conditions layout plus "no forecast" text)
// that can never actually appear on hardware.
void previewWeatherNoCity();
void previewWeatherNoForecastYet();
#endif

} // namespace Menu
