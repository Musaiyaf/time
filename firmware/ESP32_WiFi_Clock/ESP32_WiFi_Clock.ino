// ESP32-S3 (N16R8) + 1.9" ST7789 320x170 WiFi grid clock.
//
// - Connects to WiFi using credentials saved in NVS.
// - If none are saved (or the saved network can't be reached), starts a
//   "ESP32-Clock-Setup-XXXX" Access Point with a captive setup page:
//   scan/pick a network (or type one manually), enter the password, and
//   optionally a POSIX time zone + NTP servers. Saving reboots the clock,
//   which then connects and syncs time over NTP.
// - Hold the OK button (GPIO0/BOOT) for 3s at power-up to wipe saved WiFi
//   settings and return to setup mode.
// - While the clock is running: LEFT/RIGHT tap cycles clock faces; holding
//   OK opens an on-device settings menu (WiFi Setup, Time Zone) navigated
//   with the same three buttons - see menu.h/menu.cpp.
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
#include "menu.h"

WebServer server(80);
DNSServer dnsServer;

bool staMode = false; // true = connected as a WiFi client, false = setup AP
unsigned long staConnectedAt = 0;

void setup() {
  Serial.begin(115200);
  pinMode(BTN_OK_PIN, INPUT_PULLUP);

  ClockDisplay::begin();
  ClockDisplay::showBootMessage("ESP32 Grid Clock", "Starting...");

  WifiManager::begin();

  // Hold the OK button for 3s right after boot to wipe saved WiFi. This
  // only runs once, here, before Menu::begin() sets up button polling for
  // normal (post-boot) use.
  if (digitalRead(BTN_OK_PIN) == LOW) {
    ClockDisplay::showBootMessage("Keep holding to reset WiFi...", "");
    unsigned long t0 = millis();
    while (digitalRead(BTN_OK_PIN) == LOW && millis() - t0 < WIFI_RESET_HOLD_MS) {
      delay(10);
    }
    if (millis() - t0 >= WIFI_RESET_HOLD_MS) {
      WifiManager::clearCredentials();
      ClockDisplay::showBootMessage("WiFi settings cleared", "");
      delay(1000);
    }
  }

  Menu::begin();

  String ssid, pass;
  bool haveCreds = WifiManager::loadCredentials(ssid, pass);
  bool connected = false;

  if (haveCreds) {
    ClockDisplay::showBootMessage("Connecting to", ssid);
    connected = WifiManager::connectSTA(ssid, pass, WIFI_CONNECT_TIMEOUT_MS);
  }

  if (connected) {
    staMode = true;
    staConnectedAt = millis();
    ClockDisplay::showBootMessage("Connected!", "Waiting for time sync...");
    WifiManager::syncTime();
    // Start the portal immediately (not gated on NTP) so the clock's IP is
    // reachable - e.g. to check status or change WiFi/time zone - even if
    // NTP is slow or blocked on this network.
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
  static unsigned long lastWaitMsg = 0;
  static bool timeEverSynced = false;
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED && now - lastWifiCheck > 15000) {
    lastWifiCheck = now;
    WiFi.reconnect();
  }

  // Reads LEFT/RIGHT/OK every loop() iteration (not throttled like the
  // clock render below) so button presses feel responsive. Returns true
  // while a menu screen is showing, in which case skip the clock render.
  if (Menu::handle()) {
    return;
  }

  if (now - lastRender >= 200) {
    lastRender = now;
    struct tm timeinfo;
    bool timeValid = getLocalTime(&timeinfo, 5);

    if (timeValid) {
      timeEverSynced = true;
      ClockDisplay::update(timeinfo, true, WiFi.status() == WL_CONNECTED, WiFi.RSSI());
    } else if (!timeEverSynced && now - lastWaitMsg >= 1000) {
      // NTP hasn't landed yet (slow or blocked on this network) - keep the
      // screen alive with live status instead of freezing on "Syncing
      // time..." forever, so it's obvious the device is still working.
      lastWaitMsg = now;
      unsigned long elapsed = (now - staConnectedAt) / 1000;
      ClockDisplay::showBootMessage(
          "Waiting for NTP... " + String(elapsed) + "s",
          "IP " + WiFi.localIP().toString());
    }
  }
}
