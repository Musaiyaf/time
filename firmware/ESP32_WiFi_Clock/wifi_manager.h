#pragma once
#include <Arduino.h>

// Loads/saves WiFi + time settings to NVS (Preferences) and drives the
// ESP32 WiFi radio between AP (setup) mode and STA (client) mode.
namespace WifiManager {

void begin();

// Credentials
bool loadCredentials(String &ssid, String &pass);
void saveCredentials(const String &ssid, const String &pass);
void clearCredentials();

// Time settings (POSIX TZ string + up to two NTP servers)
void loadTimeConfig(String &tz, String &ntp1, String &ntp2);
void saveTimeConfig(const String &tz, const String &ntp1, const String &ntp2);

// STA (client) mode
bool connectSTA(const String &ssid, const String &pass, unsigned long timeoutMs);

// Starts (or restarts, e.g. after switching networks) the mDNS responder
// so the clock is reachable at http://<MDNS_HOSTNAME>.local/ in addition
// to its IP address. Call once after connectSTA() succeeds.
void startMDNS();

// AP (setup) mode
void startAP();
void stopAP();
String getAPName();
String getAPIP();

// Applies the saved TZ/NTP settings and starts the SNTP client.
void syncTime();

} // namespace WifiManager
