#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>

namespace {
Preferences prefs;
String apName;
const char *NVS_NAMESPACE = "clockcfg";
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
  configTzTime(tz.c_str(), ntp1.c_str(), ntp2.c_str());
}

} // namespace WifiManager
