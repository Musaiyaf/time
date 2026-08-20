#pragma once
#include <Arduino.h>

// On-device settings menu, driven by three buttons (config.h BTN_LEFT_PIN /
// BTN_RIGHT_PIN / BTN_OK_PIN):
//   - On a clock face: LEFT/RIGHT cycle faces, holding OK opens this menu.
//   - Inside the menu: LEFT/RIGHT move the selection, a short tap of OK
//     confirms it, holding OK goes back a level (or exits to the clock
//     face from the top level).
// Menu contents: Settings (WiFi, Time Zone, About - all plain text lists)
// and Back. WiFi scans for nearby networks, lets you pick one and type its
// password on an on-screen keyboard, then connects. Time Zone picks a
// continent, then a specific zone within it - covers the same ~430 IANA
// zones as the web setup page. About shows the device's current IP.
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

} // namespace Menu
