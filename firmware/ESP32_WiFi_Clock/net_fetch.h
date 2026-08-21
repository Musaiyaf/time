#pragma once
#include <Arduino.h>

// One shared HTTPS GET, used by both the weather forecast (weather.cpp)
// and the holiday calendar (calendar_events.cpp) - the TLS client setup
// and error wording are identical for both, so they live here once
// rather than in each module.
namespace NetFetch {

// Fetches url and puts the response body in out. Returns false and
// fills errOut with a short, displayable reason ("No WiFi", "HTTP 404",
// "Network error", ...) on failure. Blocking: a TLS handshake plus the
// transfer, typically a second or two.
bool get(const String &url, String &out, String &errOut);

// Percent-encodes a string for use in a query parameter.
String urlEncode(const String &in);

} // namespace NetFetch
