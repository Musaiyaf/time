#pragma once
#include <Arduino.h>
#include <time.h>

// Optional DS3231 battery-backed RTC on I2C (config.h RTC_SDA_PIN/
// RTC_SCL_PIN). It keeps ticking on its own coin-cell battery through
// power loss, so Manual (offline) mode - and the last known time in
// general - survive a reboot instead of resetting to 00:00:00 every time.
//
// This hardware is entirely optional: begin() just probes for it, and
// every other call here is a safe no-op (write()) or returns false
// (read()) if nothing responded, in which case the firmware falls back to
// its existing Jan-1 placeholder behaviour.
namespace RtcBackup {

// Starts I2C and probes for a DS3231. Safe to call even if none is wired
// up. Call once from setup(), before anything else here is used.
void begin();

// True if a DS3231 responded to begin()'s probe.
bool isPresent();

// Reads the RTC's stored date/time into tm's year/month/day/hour/min/sec
// fields directly - no time zone conversion, it's exactly whatever was
// last written with write(). Returns false if there's no RTC, or its
// Oscillator Stop Flag is set (power was lost with no/dead backup
// battery, so the stored time is stale/meaningless).
bool read(struct tm &out);

// Writes tm's year/month/day/hour/min/sec fields directly to the RTC (no
// time zone conversion - store exactly what's given) and clears the
// Oscillator Stop Flag. A safe no-op if there's no RTC present.
void write(const struct tm &t);

} // namespace RtcBackup
