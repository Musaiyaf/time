// ESP32-S3 (N16R8) + 1.9" ST7789 320x170 WiFi grid clock.
//
// - Connects to WiFi using credentials saved in NVS.
// - If there's no WiFi to connect to (none saved, or the saved network
//   can't be reached), asks: try the on-device scan/pick flow again, or
//   go into Manual Mode - fully offline, clock starts at 00:00:00 on 1
//   January (of the firmware's build year) and is corrected by hand from
//   the Settings menu's Date/Time item.
// - Hold the OK button (GPIO0/BOOT) for 3s at power-up to wipe saved WiFi
//   settings and force that same "no WiFi" prompt on the next boot.
// - While the clock is running: LEFT/RIGHT tap cycles clock faces; holding
//   OK opens an on-device Settings menu (WiFi, Time Zone, Date/Time,
//   About) navigated with the same three buttons - see menu.h/menu.cpp.
//   WiFi there scans, lets you pick a network and type its password on an
//   on-screen keyboard, then connects (this also gets you out of Manual
//   Mode, and re-syncs the real time over NTP once connected).
//
// Board settings (Arduino IDE / arduino-cli):
//   Board: "ESP32S3 Dev Module"
//   PSRAM: "OPI PSRAM"
//   Flash Size: "16MB"
//   Partition Scheme: "Default 4MB with spiffs" (or any scheme with OTA off)
//
// Library dependencies: TFT_eSPI (configured via TFT_eSPI_Setup/User_Setup.h,
// see the repo README). WiFi, WebServer, DNSServer, ESPmDNS and Preferences ship with
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

bool staMode = false;    // true = connected as a WiFi client
bool manualMode = false; // true = offline, running off the local clock only
bool portalStarted = false;
unsigned long staConnectedAt = 0;

// Shared by the initial connect in setup() and by picking up a fresh
// connection made later through Settings > WiFi (including getting out of
// Manual Mode) - see the WiFi.status() check in loop().
void beginConnectedMode() {
  staMode = true;
  manualMode = false;
  staConnectedAt = millis();
  WifiManager::startMDNS();
  WifiManager::syncTime();
  if (!portalStarted) {
    // Start the portal immediately (not gated on NTP) so the clock's IP is
    // reachable - e.g. to check status or change WiFi/time zone - even if
    // NTP is slow or blocked on this network.
    WebPortal::begin(&server, &dnsServer, false);
    portalStarted = true;
  }
}

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

  // No working WiFi (none saved, or the saved network couldn't be
  // reached): let the user keep retrying the on-device scan/pick flow, or
  // go fully offline in Manual Mode instead of forcing a connection.
  while (!connected && !manualMode) {
    if (Menu::askWifiOrManual()) {
      manualMode = true;
    } else {
      ClockDisplay::showBootMessage("Pick a WiFi network...", "");
      connected = Menu::runWifiPicker();
    }
  }

  if (connected) {
    ClockDisplay::showBootMessage("Connected!", "Waiting for time sync...");
    beginConnectedMode();
  } else {
    WifiManager::enterManualMode();
    ClockDisplay::showBootMessage("Manual mode (offline)", "Set date/time in Settings");
    delay(1500);
  }
}

void loop() {
  if (staMode) {
    WebPortal::handle();
  }

  // Reads LEFT/RIGHT/OK every loop() iteration (not throttled like the
  // clock render below) so button presses feel responsive. Returns true
  // while a menu screen is showing, in which case skip the clock render.
  // Any of its blocking sub-flows (like the WiFi picker) run to completion
  // inside this single call, so WiFi.status() below is already current.
  bool menuOwnsScreen = Menu::handle();

  // Picks up a network connected mid-session through Settings > WiFi -
  // including getting out of Manual Mode - without needing a reboot.
  if (!staMode && WiFi.status() == WL_CONNECTED) {
    beginConnectedMode();
  }

  if (menuOwnsScreen) {
    return;
  }

  static unsigned long lastWifiCheck = 0;
  static unsigned long lastRender = 0;
  static unsigned long lastWaitMsg = 0;
  static bool timeEverSynced = false;
  unsigned long now = millis();

  if (staMode && WiFi.status() != WL_CONNECTED && now - lastWifiCheck > 15000) {
    lastWifiCheck = now;
    WiFi.reconnect();
  }

  if (now - lastRender >= 200) {
    lastRender = now;
    struct tm timeinfo;
    bool timeValid = getLocalTime(&timeinfo, 5);

    if (timeValid) {
      // In Manual Mode this is the offline placeholder/hand-set clock, not
      // NTP time - still valid as far as getLocalTime() is concerned, so
      // it renders the same way, just with the WiFi badge showing off.
      timeEverSynced = true;
      bool wifiUp = staMode && WiFi.status() == WL_CONNECTED;
      ClockDisplay::update(timeinfo, true, wifiUp, wifiUp ? WiFi.RSSI() : 0);
    } else if (staMode && !timeEverSynced && now - lastWaitMsg >= 1000) {
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
