#pragma once
// Enough of the ESP32 WiFi API for the drawing code, which only ever
// asks whether there's a connection and what the address is.
#include <Arduino.h>

#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
#define WIFI_AUTH_OPEN 0

class IPAddressStub {
 public:
  String toString() const { return String("192.168.1.42"); }
};

// The WiFi scan/connect side (used by menu.cpp's runWifiPicker(), which
// the preview never calls but still needs to compile) is stubbed as
// "nothing found" rather than modelled - no scene here exercises it.
class WiFiStub {
 public:
  int status() const { return connected ? WL_CONNECTED : WL_DISCONNECTED; }
  String SSID() const { return String("HomeNetwork"); }
  String SSID(int) const { return String(""); }
  int RSSI() const { return -58; }
  int RSSI(int) const { return 0; }
  IPAddressStub localIP() const { return IPAddressStub(); }
  int scanNetworks(bool = false, bool = false) { return 0; }
  void scanDelete() {}
  int encryptionType(int) const { return WIFI_AUTH_OPEN; }

  // Host-only knob, so a scene can render the offline variants too.
  bool connected = true;
};

extern WiFiStub WiFi;
