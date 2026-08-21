#include "net_fetch.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

namespace NetFetch {

String urlEncode(const String &in) {
  String out;
  out.reserve(in.length() * 3);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}

bool get(const String &url, String &out, String &errOut) {
  if (WiFi.status() != WL_CONNECTED) {
    errOut = "No WiFi";
    return false;
  }

  WiFiClientSecure client;
  // No certificate validation. Everything fetched this way is public,
  // read-only data, and nothing sensitive goes out on the connection.
  // Pinning a CA root here would mean re-flashing the clock whenever
  // that root rotates - a poor trade for a device with no other way to
  // update itself.
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(NET_HTTP_TIMEOUT_MS);
  http.setTimeout(NET_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    errOut = "Connect failed";
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    errOut = (code < 0) ? String("Network error") : ("HTTP " + String(code));
    http.end();
    return false;
  }
  out = http.getString();
  http.end();
  if (out.length() == 0) {
    errOut = "Empty reply";
    return false;
  }
  return true;
}

} // namespace NetFetch
