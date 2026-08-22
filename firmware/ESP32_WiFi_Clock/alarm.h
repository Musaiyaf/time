#pragma once
#include <Arduino.h>
#include <time.h>

// A single daily alarm (HH:MM, on/off), persisted across power cycles.
// Settings > Alarm sets it; ESP32_WiFi_Clock.ino's loop() calls checkDue()
// once a minute and, when it fires, shows a fullscreen ringing screen
// (Menu::runAlarmRingingScreen()) until dismissed by any button.
namespace Alarm {

void begin();

bool isEnabled();
void setEnabled(bool on);

int hour();
int minute();
void setTime(int h, int m);

// Call once per loop() tick with the current local time. Edge-triggered:
// returns true at most once per day, the first tick that matches the
// alarm's hour/minute while enabled - never retriggers later the same
// minute, or again until the next day comes back around to it.
bool checkDue(const struct tm &timeinfo);

} // namespace Alarm
