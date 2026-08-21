#include "buzzer.h"
#include "config.h"
#include <Preferences.h>

namespace {
const char *NVS_NAMESPACE = "clockcfg";
Preferences prefs;
bool enabled = true;

// Short and high enough to read as a "click" rather than a beep - long
// enough to actually be audible on a small piezo, short enough not to
// blur two quick taps together. tone() queues this on its own FreeRTOS
// task (see the ESP32 core's Tone.cpp) and returns immediately, so this
// never blocks the button poll it's called from.
const unsigned int CLICK_HZ = 2200;
const unsigned long CLICK_MS = 12;
} // namespace

namespace Buzzer {

void begin() {
  pinMode(BUZZER_PIN, OUTPUT);
  prefs.begin(NVS_NAMESPACE, true);
  enabled = prefs.getBool("buzzer_on", true);
  prefs.end();
}

void click() {
  if (!enabled) return;
  tone(BUZZER_PIN, CLICK_HZ, CLICK_MS);
}

void setEnabled(bool on) {
  enabled = on;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBool("buzzer_on", on);
  prefs.end();
}

bool isEnabled() { return enabled; }

} // namespace Buzzer
