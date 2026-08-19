// ESP32-S3 (N16R8) + 1.9" ST7789 320x170 WiFi grid clock.
//
// - Connects to WiFi using credentials saved in NVS.
// - If none are saved (or the saved network can't be reached), starts a
//   "ESP32-Clock-Setup-XXXX" Access Point with a captive setup page:
//   scan/pick a network (or type one manually), enter the password, and
//   optionally a POSIX time zone + NTP servers. Saving reboots the clock,
//   which then connects and syncs time over NTP.
// - Hold the BOOT button (GPIO0) for 3s at power-up to wipe saved WiFi
//   settings and return to setup mode.
//
// Board settings (Arduino IDE / arduino-cli):
//   Board: "ESP32S3 Dev Module"
//   PSRAM: "OPI PSRAM"
//   Flash Size: "16MB"
//   Partition Scheme: "Default 4MB with spiffs" (or any scheme with OTA off)
//
// Library dependencies: TFT_eSPI (configured via TFT_eSPI_Setup/User_Setup.h,
// see the repo README). WiFi, WebServer, DNSServer and Preferences ship with
// the ESP32 Arduino core.

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

#include "config.h"
#include "wifi_manager.h"
#include "web_portal.h"
#include "clock_display.h"

WebServer server(80);
DNSServer dnsServer;

bool staMode = false; // true = connected as a WiFi client, false = setup AP

void setup() {
  Serial.begin(115200);
  pinMode(WIFI_RESET_BUTTON_PIN, INPUT_PULLUP);

  ClockDisplay::begin();
  ClockDisplay::showBootMessage("ESP32 Grid Clock", "Starting...");

  WifiManager::begin();

  // Hold the reset button for 3s right after boot to wipe saved WiFi.
  if (digitalRead(WIFI_RESET_BUTTON_PIN) == LOW) {
    ClockDisplay::showBootMessage("Keep holding to reset WiFi...", "");
    unsigned long t0 = millis();
    while (digitalRead(WIFI_RESET_BUTTON_PIN) == LOW && millis() - t0 < 3000) {
      delay(10);
    }
    if (millis() - t0 >= 3000) {
      WifiManager::clearCredentials();
      ClockDisplay::showBootMessage("WiFi settings cleared", "");
      delay(1000);
    }
  }

  String ssid, pass;
  bool haveCreds = WifiManager::loadCredentials(ssid, pass);
  bool connected = false;

  if (haveCreds) {
    ClockDisplay::showBootMessage("Connecting to", ssid);
    connected = WifiManager::connectSTA(ssid, pass, WIFI_CONNECT_TIMEOUT_MS);
  }

  if (connected) {
    staMode = true;
    ClockDisplay::showBootMessage("Connected!", "Syncing time...");
    WifiManager::syncTime();

    // Give NTP a brief window to land before showing the clock face, so
    // it doesn't flash 1970 for a second.
    struct tm ti;
    unsigned long t0 = millis();
    while (!getLocalTime(&ti, 500) && millis() - t0 < 8000) {
      // keep waiting
    }

    WebPortal::begin(&server, &dnsServer, false);
  } else {
    staMode = false;
    WifiManager::startAP();
    WebPortal::begin(&server, &dnsServer, true);
    ClockDisplay::showSetupScreen(WifiManager::getAPName(), WifiManager::getAPIP());
  }
}

void loop() {
  WebPortal::handle();

  if (!staMode) {
    return; // sitting in setup mode; the portal handles everything
  }

  static unsigned long lastWifiCheck = 0;
  static unsigned long lastRender = 0;
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED && now - lastWifiCheck > 15000) {
    lastWifiCheck = now;
    WiFi.reconnect();
  }

  if (now - lastRender >= 200) {
    lastRender = now;
    struct tm timeinfo;
    bool timeValid = getLocalTime(&timeinfo, 5);
    ClockDisplay::update(timeinfo, timeValid, WiFi.status() == WL_CONNECTED, WiFi.RSSI());
  }
}
