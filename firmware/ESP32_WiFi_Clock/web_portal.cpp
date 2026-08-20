#include "web_portal.h"
#include "webpage_html.h"
#include "wifi_manager.h"
#include "sd_card.h"
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

// ---- Video Face upload -------------------------------------------------
// The browser does all the real work (decoding the source video, letting
// the user crop/zoom/pick a length, extracting frames to raw RGB565, see
// webpage_html.h's video section) and uploads one already-finished
// container file: 4-byte "VID1" magic, little-endian uint16
// width/height/frameCount/fps, then frameCount raw RGB565 frames back to
// back. This endpoint only ever streams that file straight to the SD
// card - see video_player.h for how it's read back for playback, which
// happens independently of this web portal (or WiFi) still being up.
const char *VIDEO_PATH = "/video/video.bin";

// Called once the upload transfer completes (after handleVideoUploadData
// has streamed every chunk to SD) - just reports success/failure.
void handleVideoUpload() {
  server->send(SdCard::isPresent() ? 200 : 400, "text/plain",
               SdCard::isPresent() ? "OK" : "No SD card");
}

void handleVideoUploadData() {
  HTTPUpload &upload = server->upload();
  if (upload.status == UPLOAD_FILE_START) {
    SdCard::beginWrite(VIDEO_PATH);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    SdCard::writeChunk(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    SdCard::endWrite();
  }
}

void handleVideoStatus() {
  bool present = SdCard::exists(VIDEO_PATH);
  server->send(200, "application/json", present ? "{\"present\":true}" : "{\"present\":false}");
}

void handleVideoDelete() {
  SdCard::remove(VIDEO_PATH);
  server->send(200, "text/plain", "OK");
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
  server->on("/video/upload", HTTP_POST, handleVideoUpload, handleVideoUploadData);
  server->on("/video/status", HTTP_GET, handleVideoStatus);
  server->on("/video/delete", HTTP_POST, handleVideoDelete);

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
