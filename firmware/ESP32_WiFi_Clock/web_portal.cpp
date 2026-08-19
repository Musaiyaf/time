#include "web_portal.h"
#include "webpage_html.h"
#include "wifi_manager.h"
#include "config.h"
#include <WiFi.h>

namespace {

WebServer *server = nullptr;
DNSServer *dns = nullptr;
bool captive = false;

String escapeJson(const String &in) {
  String out;
  out.reserve(in.length() + 4);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out;
}

void handleRoot() {
  String page;
  page.reserve(strlen_P(PAGE_TEMPLATE) + 256);
  page = FPSTR(PAGE_TEMPLATE);

  String tz, ntp1, ntp2;
  WifiManager::loadTimeConfig(tz, ntp1, ntp2);

  String banner;
  String status;
  if (captive) {
    banner = "<div class=\"banner\">Setup mode &mdash; the clock is not connected to WiFi yet.</div>";
  } else {
    banner = "<div class=\"banner\">Connected to <b>" + WiFi.SSID() + "</b> &middot; IP " +
             WiFi.localIP().toString() + "</div>";
    status = "Currently connected. Saving new details will reconnect the clock.";
  }

  page.replace("%BANNER%", banner);
  page.replace("%STATUS%", status);
  page.replace("%TZ%", tz);
  page.replace("%NTP1%", ntp1);
  page.replace("%NTP2%", ntp2);

  server->send(200, "text/html", page);
}

void handleScan() {
  int n = WiFi.scanNetworks(false, true);
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    json += "{\"ssid\":\"" + escapeJson(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  json += "]";
  WiFi.scanDelete();
  server->send(200, "application/json", json);
}

void handleSave() {
  String ssid = server->arg("ssid");
  String pass = server->arg("pass");
  String tz = server->arg("tz");
  String ntp1 = server->arg("ntp1");
  String ntp2 = server->arg("ntp2");

  if (ssid.length() == 0) {
    server->send(400, "text/plain", "SSID required");
    return;
  }
  if (tz.length() == 0) tz = DEFAULT_POSIX_TZ;
  if (ntp1.length() == 0) ntp1 = DEFAULT_NTP_SERVER1;
  if (ntp2.length() == 0) ntp2 = DEFAULT_NTP_SERVER2;

  WifiManager::saveCredentials(ssid, pass);
  WifiManager::saveTimeConfig(tz, ntp1, ntp2);

  server->send(200, "text/plain", "OK");
  delay(600);
  ESP.restart();
}

void handleResetWifi() {
  WifiManager::clearCredentials();
  server->send(200, "text/plain", "OK");
  delay(600);
  ESP.restart();
}

void handleCaptivePing() {
  // Common captive-portal probe URLs (Android/iOS/Windows). Redirecting
  // them to "/" makes the setup page pop up automatically on most phones.
  String ip = captive ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  server->sendHeader("Location", "http://" + ip + "/", true);
  server->send(302, "text/plain", "");
}

void handleNotFound() {
  if (captive) {
    handleCaptivePing();
  } else {
    server->send(404, "text/plain", "Not found");
  }
}

} // namespace

namespace WebPortal {

void begin(WebServer *serverPtr, DNSServer *dnsPtr, bool isCaptive) {
  server = serverPtr;
  dns = dnsPtr;
  captive = isCaptive;

  server->on("/", HTTP_GET, handleRoot);
  server->on("/scan", HTTP_GET, handleScan);
  server->on("/save", HTTP_POST, handleSave);
  server->on("/resetwifi", HTTP_GET, handleResetWifi);

  // Captive portal probe endpoints used by various OSes.
  server->on("/generate_204", HTTP_GET, handleCaptivePing);       // Android
  server->on("/gen_204", HTTP_GET, handleCaptivePing);            // Android
  server->on("/hotspot-detect.html", HTTP_GET, handleCaptivePing);// iOS/macOS
  server->on("/ncsi.txt", HTTP_GET, handleCaptivePing);           // Windows
  server->on("/connecttest.txt", HTTP_GET, handleCaptivePing);    // Windows

  server->onNotFound(handleNotFound);
  server->begin();

  if (captive) {
    dns->start(53, "*", WiFi.softAPIP());
  }
}

void handle() {
  if (captive && dns) dns->processNextRequest();
  if (server) server->handleClient();
}

} // namespace WebPortal
