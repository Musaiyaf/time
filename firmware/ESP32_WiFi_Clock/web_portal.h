#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>

// The setup / status web UI. Serves the same page in both AP (setup) and
// STA (already connected) modes; the JS on the page adapts.
namespace WebPortal {

// isCaptive = true when running as the setup Access Point (adds a DNS
// server so phones auto-detect a captive portal and pop the page open).
void begin(WebServer *serverPtr, DNSServer *dnsPtr, bool isCaptive);

// Call every loop() iteration.
void handle();

} // namespace WebPortal
