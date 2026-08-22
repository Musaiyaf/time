#include "alarm.h"
#include <Preferences.h>

namespace {
const char *NVS_NAMESPACE = "clockcfg";
Preferences prefs;
bool enabled = false;
int alarmHour = 7;
int alarmMinute = 0;
int lastFiredYday = -1; // day-of-year it last fired, so checkDue() only fires once
} // namespace

namespace Alarm {

void begin() {
  prefs.begin(NVS_NAMESPACE, true);
  enabled = prefs.getBool("alarm_on", false);
  alarmHour = prefs.getInt("alarm_h", 7);
  alarmMinute = prefs.getInt("alarm_m", 0);
  prefs.end();
}

bool isEnabled() { return enabled; }

void setEnabled(bool on) {
  enabled = on;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBool("alarm_on", on);
  prefs.end();
}

int hour() { return alarmHour; }
int minute() { return alarmMinute; }

void setTime(int h, int m) {
  alarmHour = h;
  alarmMinute = m;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putInt("alarm_h", h);
  prefs.putInt("alarm_m", m);
  prefs.end();
}

bool checkDue(const struct tm &timeinfo) {
  if (!enabled) return false;
  if (timeinfo.tm_hour != alarmHour || timeinfo.tm_min != alarmMinute) return false;
  if (lastFiredYday == timeinfo.tm_yday) return false;
  lastFiredYday = timeinfo.tm_yday;
  return true;
}

} // namespace Alarm
