#include "web_portal.h"
#include "webpage_html.h"
#include "wifi_manager.h"
#include "sd_card.h"
#include "video_player.h"
#include "clock_display.h"
#include "weather.h"
#include "custom_face.h"
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
  page.replace("%CITY%", Weather::cityName());

  // Without this, a browser is free to keep serving an old cached copy of
  // this page indefinitely - happened in practice with the Video
  // Wallpaper card: after its extraction size changed (140 -> 170 tall),
  // a phone that had visited before kept running the *old* cached JS,
  // silently re-uploading videos at the old size forever with no error,
  // no matter how many times firmware-side fixes were reflashed.
  server->sendHeader("Cache-Control", "no-store");
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

// Total expected upload size, captured from the request's Content-Length
// at UPLOAD_FILE_START - the multipart envelope adds a small fixed
// overhead around the actual file, close enough for a progress percentage.
size_t uploadTotalBytes = 0;
int uploadLastPercentShown = -1;

// Draws a simple "Receiving <label>... NN%" screen directly on the shared
// display (see ClockDisplay::rawDisplay(), the same escape hatch the
// on-device menu uses to draw its own screens) - the clock face has no
// idea an upload is even happening, so this is the only way to surface
// progress on-device rather than just in the browser tab. Shared by the
// Video and Custom Face uploads below - only the label differs.
void drawUploadProgress(int percent, const char *label) {
  TFT_eSPI &tft = ClockDisplay::rawDisplay();
  int w = TFT_SCREEN_WIDTH, h = TFT_SCREEN_HEIGHT;

  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(label, w / 2, h / 2 - 28);

  int barW = w - 60, barH = 18;
  int barX = 30, barY = h / 2 - 4;
  tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
  int fillW = (barW - 4) * percent / 100;
  tft.fillRect(barX + 2, barY + 2, fillW, barH - 4, TFT_CYAN);
  tft.fillRect(barX + 2 + fillW, barY + 2, (barW - 4) - fillW, barH - 4, TFT_BLACK);

  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.drawString(String(percent) + "%", w / 2, barY + barH + 18);
  tft.setFreeFont(nullptr);
}

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
    uploadTotalBytes = server->header("Content-Length").toInt();
    uploadLastPercentShown = -1;
    drawUploadProgress(0, "Receiving video...");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    SdCard::writeChunk(upload.buf, upload.currentSize);
    if (uploadTotalBytes > 0) {
      int percent = (int)((upload.totalSize * 100ULL) / uploadTotalBytes);
      if (percent > 100) percent = 100;
      // Redrawing on every chunk (there are thousands, at ~1.4KB each for
      // a multi-MB upload) would badly slow the transfer down with SPI
      // traffic - only repaint when the shown percentage actually changes.
      if (percent != uploadLastPercentShown) {
        uploadLastPercentShown = percent;
        drawUploadProgress(percent, "Receiving video...");
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    SdCard::endWrite();
    // A new file is on SD now - forget whatever header VideoPlayer had
    // cached (possibly from this very file's previous, differently-sized
    // version) so Video Face picks up the fresh one on its next redraw
    // instead of judging it against stale state until a reboot.
    VideoPlayer::invalidate();
    drawUploadProgress(100, "Receiving video...");
    delay(400); // brief, so "100%" is actually visible before it clears
    ClockDisplay::forceFullRedraw();
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

// ---- Custom Face uploads ------------------------------------------------
// Files are built entirely off-device by tools/make_custom_face.html (see
// custom_face.h for the exact binary layout this endpoint never inspects,
// just streams to SD) - the same generic upload-to-SD approach as Video
// Face's above. Unlike Video Face there can be several saved at once, so
// this section also lists and deletes; which one is *active* is picked
// on-device instead, from Settings > Custom Face (menu.cpp's
// runCustomFacePicker()) - this portal only manages which files exist.
const char *FACES_DIR = "/faces/";
String faceUploadFilename;

// Keeps an uploaded filename SD-safe and inside /faces - strips any path
// separators from what the browser sent (a full path on some browsers,
// just a name on others) and forces a .cface extension so a renamed file
// doesn't silently fail to show up in the on-device picker, which only
// lists *.cface.
String sanitizeFaceFilename(const String &rawName) {
  String name = rawName;
  int slash = max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
  if (slash >= 0) name = name.substring(slash + 1);
  name.trim();
  if (name.length() == 0) name = "face";
  String lower = name;
  lower.toLowerCase();
  if (!lower.endsWith(".cface")) name += ".cface";
  return name;
}

void handleFaceList() {
  CustomFace::Entry entries[40];
  int count = CustomFace::list(entries, 40);
  String json = "[";
  for (int i = 0; i < count; i++) {
    if (i) json += ",";
    String file = entries[i].path.substring(strlen(FACES_DIR));
    json += "{\"name\":\"" + escapeJson(entries[i].displayName) + "\",";
    json += "\"file\":\"" + escapeJson(file) + "\",";
    json += "\"active\":" + String(CustomFace::hasActive() && CustomFace::activePath() == entries[i].path
                                        ? "true" : "false") + "}";
  }
  json += "]";
  server->send(200, "application/json", json);
}

void handleFaceUpload() {
  server->send(SdCard::isPresent() ? 200 : 400, "text/plain",
               SdCard::isPresent() ? "OK" : "No SD card");
}

void handleFaceUploadData() {
  HTTPUpload &upload = server->upload();
  if (upload.status == UPLOAD_FILE_START) {
    faceUploadFilename = sanitizeFaceFilename(upload.filename);
    SdCard::beginWrite(String(FACES_DIR) + faceUploadFilename);
    uploadTotalBytes = server->header("Content-Length").toInt();
    uploadLastPercentShown = -1;
    drawUploadProgress(0, "Receiving custom face...");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    SdCard::writeChunk(upload.buf, upload.currentSize);
    if (uploadTotalBytes > 0) {
      int percent = (int)((upload.totalSize * 100ULL) / uploadTotalBytes);
      if (percent > 100) percent = 100;
      if (percent != uploadLastPercentShown) {
        uploadLastPercentShown = percent;
        drawUploadProgress(percent, "Receiving custom face...");
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    SdCard::endWrite();
    drawUploadProgress(100, "Receiving custom face...");
    delay(400);
    ClockDisplay::forceFullRedraw();
  }
}

void handleFaceDelete() {
  String file = server->arg("file");
  if (file.length() == 0) {
    server->send(400, "text/plain", "file required");
    return;
  }
  String path = String(FACES_DIR) + sanitizeFaceFilename(file);
  // Deleting the file the Custom clock face is currently showing needs the
  // same "fall back to the placeholder message" cleanup
  // CustomFace::clearActive() already does for the on-device picker's
  // "None" entry, plus a repaint in case that face is on screen right now.
  bool wasActive = CustomFace::hasActive() && CustomFace::activePath() == path;
  SdCard::remove(path);
  if (wasActive) {
    CustomFace::clearActive();
    ClockDisplay::forceFullRedraw();
  }
  server->send(200, "text/plain", "OK");
}

// ---- Weather city ------------------------------------------------------
// Saving is deliberately not part of the WiFi form's save-and-reboot
// flow: the city only needs a geocoding lookup, which works right away on
// an already-connected clock, so this applies immediately and the page
// stays put. In AP/setup mode there's no internet to look a name up
// against yet, so it says so instead of failing cryptically.
void handleWeatherStatus() {
  String json = "{\"city\":\"" + escapeJson(Weather::cityName()) + "\",";
  json += "\"label\":\"" + escapeJson(Weather::resolvedLabel()) + "\",";
  json += "\"status\":\"" + escapeJson(Weather::statusText()) + "\",";
  json += "\"online\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + "}";
  server->send(200, "application/json", json);
}

void handleWeatherSave() {
  String city = server->arg("city");
  city.trim();

  if (city.length() == 0) {
    Weather::clearCity();
    server->send(200, "text/plain", "Weather city cleared.");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    server->send(409, "text/plain",
                 "The clock needs to be on WiFi first - the city name has to be looked up online.");
    return;
  }

  String err;
  if (Weather::setCity(city, err)) {
    server->send(200, "text/plain", "Saved: " + Weather::resolvedLabel());
  } else {
    server->send(400, "text/plain", err);
  }
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

  // WebServer only exposes headers explicitly registered here - without
  // this, server->header("Content-Length") in handleVideoUploadData()
  // would always come back empty, and the upload progress bar could
  // never compute a percentage.
  static const char *collectedHeaders[] = {"Content-Length"};
  server->collectHeaders(collectedHeaders, 1);

  server->on("/", HTTP_GET, handleRoot);
  server->on("/scan", HTTP_GET, handleScan);
  server->on("/save", HTTP_POST, handleSave);
  server->on("/resetwifi", HTTP_GET, handleResetWifi);
  server->on("/video/upload", HTTP_POST, handleVideoUpload, handleVideoUploadData);
  server->on("/video/status", HTTP_GET, handleVideoStatus);
  server->on("/video/delete", HTTP_POST, handleVideoDelete);
  server->on("/faces/list", HTTP_GET, handleFaceList);
  server->on("/faces/upload", HTTP_POST, handleFaceUpload, handleFaceUploadData);
  server->on("/faces/delete", HTTP_POST, handleFaceDelete);
  server->on("/weather/status", HTTP_GET, handleWeatherStatus);
  server->on("/weather/save", HTTP_POST, handleWeatherSave);

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
