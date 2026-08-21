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
// About - all plain text lists) and Back. WiFi scans for nearby networks,
// lets you pick one and type its password on an on-screen keyboard, then
// connects. Time Zone picks a continent, then a specific zone within it -
// covers the same ~430 IANA zones as the web setup page. Date/Time sets
// the clock by hand, field by field - used for Manual (offline) mode, or
// just to correct the time. Weather City types a city name on the same
// on-screen keyboard and looks it up. About shows the device's current
// IP/hostname.
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

} // namespace Menu
