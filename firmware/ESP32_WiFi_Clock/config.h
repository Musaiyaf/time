#pragma once

// =====================================================================
// TFT pin wiring (ESP32-S3-N16R8  <->  1.9" ST7789 320x170 IPS panel)
//
// These MUST match TFT_eSPI_Setup/User_Setup.h (used at compile time by
// the TFT_eSPI library). If you rewire the display, change the pins in
// BOTH places.
//
// Pins were chosen to avoid ESP32-S3 strapping pins (0, 3, 45, 46),
// the USB D+/D- pins (19, 20) and the pins reserved for the internal
// SPI flash / octal PSRAM on N16R8 modules (26-37). Any other free
// GPIO can be substituted.
// =====================================================================
#define TFT_SCLK_PIN 12   // SPI clock
#define TFT_MOSI_PIN 11   // SPI data (SDA/MOSI on the panel)
#define TFT_CS_PIN   10   // Chip select
#define TFT_DC_PIN   13   // Data/command
#define TFT_RST_PIN  14   // Reset
#define TFT_BL_PIN    2   // Backlight (LED/BLK)

#define TFT_SCREEN_WIDTH  320
#define TFT_SCREEN_HEIGHT 170

// ---------------------------------------------------------------------
// Three-button navigation: LEFT / RIGHT cycle clock faces (or move the
// selection inside a menu); OK opens/confirms things. All three are
// buttons to GND, read with INPUT_PULLUP (so idle = HIGH, pressed = LOW).
// BTN_LEFT_PIN/BTN_RIGHT_PIN need wiring to two free GPIOs - change these
// to match if you wire them elsewhere. BTN_OK_PIN reuses the existing
// BOOT button, so no extra wiring is needed for it.
// ---------------------------------------------------------------------
#define BTN_LEFT_PIN  4
#define BTN_RIGHT_PIN 5
#define BTN_OK_PIN    0   // BOOT button on most ESP32-S3 dev boards

// Hold BTN_OK_PIN LOW (button to GND) for 3s right after power-up to wipe
// the saved WiFi credentials and force the setup Access Point back on.
// This is separate from (and checked before) the menu system below - it
// only runs once, at boot, before loop() and the Menu module exist.
#define WIFI_RESET_HOLD_MS 3000UL

// ---------------------------------------------------------------------
// Setup Access Point (shown when there are no saved / working credentials)
// ---------------------------------------------------------------------
#define AP_SSID_PREFIX "ESP32-Clock-Setup"   // + last 3 bytes of MAC
#define AP_PASSWORD    ""                     // "" = open network. Min 8 chars if set.

// ---------------------------------------------------------------------
// Time defaults (all overridable from the web setup page, saved to NVS)
// ---------------------------------------------------------------------
#define DEFAULT_NTP_SERVER1 "pool.ntp.org"
#define DEFAULT_NTP_SERVER2 "time.nist.gov"
// Fixed third NTP fallback, always used in addition to the two above (not
// user-configurable). Given as a raw IP so it still works if DNS on the
// local network is broken/blocked even though NTP itself isn't - Cloudflare's
// anycast time service.
#define FALLBACK_NTP_SERVER3 "162.159.200.1" // time.cloudflare.com
// POSIX TZ string, see https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h
// Examples: "UTC0", "EST5EDT,M3.2.0,M11.1.0" (US Eastern), "CST-8" (China),
//           "GMT0BST,M3.5.0/1,M10.5.0" (UK)
#define DEFAULT_POSIX_TZ "UTC0"

#define WIFI_CONNECT_TIMEOUT_MS 15000UL

// ---------------------------------------------------------------------
// mDNS: once connected to WiFi, the clock is also reachable at
// http://<MDNS_HOSTNAME>.local/ - no need to look up its IP address.
// (Only resolves on networks/clients that support mDNS/Bonjour; most
// phones, Macs and Linux do out of the box, some Windows/router setups
// don't - the IP address on the About screen still works everywhere.)
// ---------------------------------------------------------------------
#define MDNS_HOSTNAME "esp32-clock"
