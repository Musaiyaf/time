#include <Arduino.h>

namespace {
// Frozen wall clock, so two runs of the same scene render identically and
// a diff between them means a real change.
struct tm fakeNow = {};
bool fakeNowSet = false;
} // namespace

unsigned long millis() { return 1000; }
void delay(unsigned long) {}
void pinMode(int, int) {}
int digitalRead(int) { return HIGH; } // buttons idle (INPUT_PULLUP)
void digitalWrite(int, int) {}
int digitalPinToInterrupt(int pin) { return pin; }
void attachInterrupt(int, void (*)(), int) {}
void noInterrupts() {}
void interrupts() {}

void hostSetFakeNow(int year, int month, int day, int hour, int minute, int second) {
  fakeNow = {};
  fakeNow.tm_year = year - 1900;
  fakeNow.tm_mon = month - 1;
  fakeNow.tm_mday = day;
  fakeNow.tm_hour = hour;
  fakeNow.tm_min = minute;
  fakeNow.tm_sec = second;
  fakeNow.tm_isdst = -1;
  // Normalise so tm_wday/tm_yday are right - the calendar grid uses them.
  time_t t = mktime(&fakeNow);
  fakeNow = *localtime(&t);
  fakeNowSet = true;
}

bool getLocalTime(struct tm *info, uint32_t) {
  if (!fakeNowSet) hostSetFakeNow(2026, 8, 21, 12, 34, 56);
  *info = fakeNow;
  return true;
}
