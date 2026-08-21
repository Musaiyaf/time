#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>

// Renders the "grid clock" theme: a row of colored info badges (date /
// weekday / day-of-year / WiFi signal) above a large grid of digit cells
// showing HH:MM:SS.
namespace ClockDisplay {

void begin();

// Switches to the next/previous clock face (rainbow grid -> retro LED
// display -> Custom -> Video -> Photo -> Botanical -> Deco -> back to
// rainbow grid, or the reverse). Call from a button tap; takes effect on
// the next update() call.
void nextFace();
void prevFace();

// Forces the next update() call to repaint the whole clock area and every
// badge, even if nothing has actually changed. Call this after drawing
// something else over the screen (like a menu) so the clock face redraws
// cleanly on top of it instead of leaving stale menu pixels behind.
void forceFullRedraw();

// The single shared TFT_eSPI instance, already init()'d and rotated - for
// other modules (like the on-device settings menu) that need to draw their
// own screens on top of / instead of the clock face, without creating a
// second display driver instance.
TFT_eSPI &rawDisplay();

// Simple centered status text, used while booting / connecting / in
// setup mode (before the clock face is shown).
void showBootMessage(const String &line1, const String &line2 = "");

// Full setup-mode screen: shows the AP SSID and IP address to connect to.
void showSetupScreen(const String &apName, const String &apIP);

// Redraws the clock face. Call repeatedly from loop(); it only repaints
// when the visible second actually changes, so it's safe to call often.
void update(const struct tm &timeinfo, bool timeValid, bool wifiConnected, int rssi);

} // namespace ClockDisplay
