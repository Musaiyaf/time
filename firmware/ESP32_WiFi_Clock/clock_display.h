#pragma once
#include <Arduino.h>
#include <time.h>

// Renders the "grid clock" theme: a row of colored info badges (date /
// weekday / day-of-year / WiFi signal) above a large grid of digit cells
// showing HH:MM:SS.
namespace ClockDisplay {

void begin();

// Switches to the next clock face (rainbow grid -> retro flip-clock -> ...).
// Call this from a BOOT-button tap; takes effect on the next update() call.
void nextFace();

// Simple centered status text, used while booting / connecting / in
// setup mode (before the clock face is shown).
void showBootMessage(const String &line1, const String &line2 = "");

// Full setup-mode screen: shows the AP SSID and IP address to connect to.
void showSetupScreen(const String &apName, const String &apIP);

// Redraws the clock face. Call repeatedly from loop(); it only repaints
// when the visible second actually changes, so it's safe to call often.
void update(const struct tm &timeinfo, bool timeValid, bool wifiConnected, int rssi);

} // namespace ClockDisplay
