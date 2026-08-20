#include "rtc_backup.h"
#include "config.h"
#include <Wire.h>

namespace {
const uint8_t DS3231_ADDR = 0x68;
const uint8_t REG_TIME = 0x00;   // seconds..year, 7 bytes
const uint8_t REG_STATUS = 0x0F; // bit 7 = Oscillator Stop Flag
bool present = false;

uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((int)DS3231_ADDR, 1);
  return Wire.available() ? Wire.read() : 0;
}
} // namespace

namespace RtcBackup {

void begin() {
  Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);
  Wire.beginTransmission(DS3231_ADDR);
  present = (Wire.endTransmission() == 0);
}

bool isPresent() { return present; }

bool read(struct tm &out) {
  if (!present) return false;
  if (readReg(REG_STATUS) & 0x80) return false; // oscillator stopped - stale

  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(REG_TIME);
  Wire.endTransmission(false);
  Wire.requestFrom((int)DS3231_ADDR, 7);
  if (Wire.available() < 7) return false;

  uint8_t sec = Wire.read();
  uint8_t minute = Wire.read();
  uint8_t hour = Wire.read();
  Wire.read(); // day-of-week register - unused by this firmware
  uint8_t date = Wire.read();
  uint8_t month = Wire.read();
  uint8_t year = Wire.read();

  out.tm_sec = bcd2dec(sec & 0x7F);
  out.tm_min = bcd2dec(minute & 0x7F);
  out.tm_hour = bcd2dec(hour & 0x3F);   // 24-hour mode, as set by write()
  out.tm_mday = bcd2dec(date & 0x3F);
  out.tm_mon = bcd2dec(month & 0x1F) - 1;
  out.tm_year = bcd2dec(year) + 100; // DS3231 stores 2 digits; assume 20xx
  out.tm_wday = 0;
  out.tm_yday = 0;
  out.tm_isdst = 0;
  return true;
}

void write(const struct tm &t) {
  if (!present) return;

  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(REG_TIME);
  Wire.write(dec2bcd(t.tm_sec));
  Wire.write(dec2bcd(t.tm_min));
  Wire.write(dec2bcd(t.tm_hour)); // bit6=0 selects 24-hour mode
  Wire.write(dec2bcd(1));         // day-of-week - unused, just needs 1-7
  Wire.write(dec2bcd(t.tm_mday));
  Wire.write(dec2bcd(t.tm_mon + 1));
  Wire.write(dec2bcd((t.tm_year + 1900) % 100));
  Wire.endTransmission();

  // Now that a known-good time is set, clear the Oscillator Stop Flag so a
  // future read() trusts it again.
  uint8_t status = readReg(REG_STATUS);
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(REG_STATUS);
  Wire.write(status & ~0x80);
  Wire.endTransmission();
}

} // namespace RtcBackup
