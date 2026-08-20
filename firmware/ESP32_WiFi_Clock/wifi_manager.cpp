#include "wifi_manager.h"
#include "config.h"
#include "rtc_backup.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

namespace {
Preferences prefs;
String apName;
const char *NVS_NAMESPACE = "clockcfg";

// Sets the system clock directly to the given wall-clock date/time,
// interpreted under whatever TZ is currently active (UTC unless
// configTzTime()/syncTime() has run this session).
void applySystemTime(int year, int month, int day, int hour, int minute, int second = 0) {
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = second;
  t.tm_isdst = -1;
  time_t epoch = mktime(&t);
  struct timeval tv = {epoch, 0};
  settimeofday(&tv, nullptr);
}
} // namespace

namespace WifiManager {

void begin() {
  // Nothing to do up front; Preferences namespace is opened per-call so
  // both setup() and the web handlers (running later) can use it safely.
}

bool loadCredentials(String &ssid, String &pass) {
  prefs.begin(NVS_NAMESPACE, true); // read-only
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  prefs.end();
  return ssid.length() > 0;
}

void saveCredentials(const String &ssid, const String &pass) {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

void clearCredentials() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
}

void loadTimeConfig(String &tz, String &ntp1, String &ntp2) {
  prefs.begin(NVS_NAMESPACE, true);
  tz = prefs.getString("tz", DEFAULT_POSIX_TZ);
  ntp1 = prefs.getString("ntp1", DEFAULT_NTP_SERVER1);
  ntp2 = prefs.getString("ntp2", DEFAULT_NTP_SERVER2);
  prefs.end();
}

void saveTimeConfig(const String &tz, const String &ntp1, const String &ntp2) {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("tz", tz);
  prefs.putString("ntp1", ntp1);
  prefs.putString("ntp2", ntp2);
  prefs.end();
}

bool connectSTA(const String &ssid, const String &pass, unsigned long timeoutMs) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

void startMDNS() {
  // MDNS.end() is a safe no-op if it was never started; calling it first
  // lets this double as a restart after the picker switches networks
  // (the responder needs to re-announce under the new IP).
  MDNS.end();
  MDNS.begin(MDNS_HOSTNAME);
  MDNS.addService("http", "tcp", 80);
}

void startAP() {
  WiFi.mode(WIFI_AP);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char suffix[8];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  apName = String(AP_SSID_PREFIX) + "-" + suffix;

  if (strlen(AP_PASSWORD) >= 8) {
    WiFi.softAP(apName.c_str(), AP_PASSWORD);
  } else {
    WiFi.softAP(apName.c_str());
  }
}

void stopAP() {
  WiFi.softAPdisconnect(true);
}

String getAPName() {
  return apName;
}

String getAPIP() {
  return WiFi.softAPIP().toString();
}

void syncTime() {
  String tz, ntp1, ntp2;
  loadTimeConfig(tz, ntp1, ntp2);
  configTzTime(tz.c_str(), ntp1.c_str(), ntp2.c_str(), FALLBACK_NTP_SERVER3);
}

void enterManualMode() {
  setenv("TZ", "UTC0", 1);
  tzset();

  struct tm t = {};
  if (!RtcBackup::read(t)) {
    // No RTC (or its battery didn't hold) - fall back to the placeholder.
    // __DATE__ is always "Mmm dd yyyy" - the year is its last 4 characters.
    const char *buildDate = __DATE__;
    int year = atoi(buildDate + strlen(buildDate) - 4);
    t.tm_year = year - 1900;
    t.tm_mon = 0;
    t.tm_mday = 1;
  }
  applySystemTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

void setManualDateTime(int year, int month, int day, int hour, int minute) {
  applySystemTime(year, month, day, hour, minute);
  backupTimeToRtc();
}

void backupTimeToRtc() {
  struct tm t;
  if (getLocalTime(&t, 5)) {
    RtcBackup::write(t);
  }
}

} // namespace WifiManager
