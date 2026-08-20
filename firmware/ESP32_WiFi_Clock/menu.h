#pragma once
#include <Arduino.h>

// On-device settings menu, driven by three buttons (config.h BTN_LEFT_PIN /
// BTN_RIGHT_PIN / BTN_OK_PIN):
//   - On a clock face: LEFT/RIGHT cycle faces, holding OK opens this menu.
//   - Inside the menu: LEFT/RIGHT move the selection, a short tap of OK
//     confirms it, holding OK goes back a level (or exits to the clock
//     face from the top level).
// Menu contents: WiFi Setup (reboots into the AP setup portal, same as
// holding OK for 3s at power-up) and Time Zone (continent, then a specific
// zone within it - covers the same ~430 IANA zones as the web setup page).
namespace Menu {

void begin();

// Call once per loop() iteration, however often that runs (not throttled
// like ClockDisplay::update()) so button presses feel responsive. Returns
// true while a menu screen owns the display - the caller should skip its
// normal clock rendering that iteration in that case.
bool handle();

} // namespace Menu
