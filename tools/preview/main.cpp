// Renders named scenes from the firmware's real drawing code (menu.cpp's
// weather/calendar screens, so far - see tools/preview/README.md) into
// PNG screenshots, entirely on the host. No device, no flashing, no
// photo needed to see whether a layout change actually worked.
//
// Usage: preview [scene ...]        (default: every scene below)
// Output: tools/preview/out/<scene>.png

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "clock_display.h"
#include "menu.h"
#include "host_scene.h"
#include "png_writer.h"

namespace {

// Every scene renders at this upscale so individual pixels are still
// visible on a normal monitor - the panel itself is only 320x170.
const int SCALE = 3;

void resetScene() { scene = HostScene(); }

void save(const char *name) {
  TFT_eSPI &tft = ClockDisplay::rawDisplay();
  std::string path = std::string("tools/preview/out/") + name + ".png";
  bool ok = png::writeRgb565(path, tft.frameBuffer(), tft.width(), tft.height(), SCALE);
  printf(ok ? "wrote %s\n" : "FAILED to write %s\n", path.c_str());
}

// ---- weather scenes -------------------------------------------------

void sceneWeatherTyphoon() {
  resetScene();
  scene.city = "Kattankudy";
  scene.cityLabel = "Kattankudy, Sri Lanka";
  scene.countryCode = "LK";
  scene.tempC = 33;
  scene.feelsC = 38;
  scene.code = 51; // Drizzle
  scene.humidity = 59;
  scene.windKph = 12;
  scene.forecastAgeSecs = 20;
  int hours[6] = {12, 13, 14, 15, 16, 17};
  float temps[6] = {32, 31, 32, 32, 30, 30};
  int codes[6] = {61, 61, 61, 2, 80, 61};
  int precip[6] = {76, 62, 88, 96, 94, 84};
  for (int i = 0; i < 6; i++) scene.addHour(hours[i], temps[i], codes[i], precip[i]);
  Menu::previewWeatherScreen(0);
  save("weather_typhoon"); // the exact numbers from the hardware photo
}

void sceneWeatherLongNames() {
  // Edge case: the longest realistic city label and a condition name
  // that's wide enough to test fitToWidth()'s ellipsis.
  resetScene();
  scene.city = "Llanfairpwllgwyngyll";
  scene.cityLabel = "Llanfairpwllgwyngyllgogerychwyrndrobwllllantysiliogogogoch, United Kingdom";
  scene.countryCode = "GB";
  scene.tempC = -3;
  scene.feelsC = -11;
  scene.code = 96; // Thunder + hail - one of the longer condition strings
  scene.humidity = 100;
  scene.windKph = 87;
  scene.forecastAgeSecs = 3 * 3600 + 200; // "3h ago"
  for (int i = 0; i < 6; i++) {
    scene.addHour(i, -5.0f + i, 71 + i, (i * 17) % 101);
  }
  Menu::previewWeatherScreen(0);
  save("weather_long_names");
}

void sceneWeatherNoCity() {
  // Real device path: runWeatherScreen() replaces the whole screen with
  // this message and never calls drawWeatherScreen() at all - see the
  // comment on Menu::previewWeatherNoCity().
  resetScene();
  scene.city = "";
  scene.cityLabel = "";
  scene.countryCode = "";
  Menu::previewWeatherNoCity();
  save("weather_no_city");
}

void sceneWeatherNoForecastYet() {
  // Real device path: same as above, but the "no forecast yet" message
  // instead - see Menu::previewWeatherNoForecastYet().
  resetScene();
  scene.hasForecast = false;
  Menu::previewWeatherNoForecastYet();
  save("weather_no_forecast_yet");
}

void sceneWeatherScrolled() {
  resetScene();
  for (int i = 0; i < 12; i++) scene.addHour((i + 18) % 24, 20 + (i % 5), 1 + i % 4, i * 8 % 101);
  Menu::previewWeatherScreen(6); // second page of the 12h strip
  save("weather_scrolled");
}

// ---- calendar scenes --------------------------------------------------

void sceneCalendarAugust2026() {
  resetScene();
  scene.countryCode = "LK";
  scene.calendarStatus = "Not available for LK"; // Nager.Date's real answer, once HTTP 204 is handled
  hostSetFakeNow(2026, 8, 21, 12, 0, 0);
  Menu::previewCalendarScreen(2026, 8);
  save("calendar_no_coverage"); // matches what's now on the hardware
}

void sceneCalendarWithFestivals() {
  resetScene();
  scene.countryCode = "US";
  hostSetFakeNow(2026, 8, 21, 12, 0, 0);
  scene.addEvent(2026, 8, 21, "Today's Own Festival"); // today, to check the today+event overlap
  scene.addEvent(2026, 9, 7, "Labour Day");
  scene.addEvent(2026, 10, 12, "Columbus Day");
  scene.addEvent(2026, 11, 11, "A Very Long Festival Name That's Cut Off");
  scene.addEvent(2026, 11, 26, "Thanksgiving Day");
  Menu::previewCalendarScreen(2026, 8);
  save("calendar_with_festivals");
}

void sceneCalendarNoCity() {
  resetScene();
  scene.countryCode = "";
  scene.calendarStatus = "No city set";
  hostSetFakeNow(2026, 8, 21, 12, 0, 0);
  Menu::previewCalendarScreen(2026, 8);
  save("calendar_no_city");
}

void sceneCalendarFebLeap() {
  // Edge case: a 29-day February, so the last week's row layout is exercised.
  resetScene();
  scene.countryCode = "US";
  hostSetFakeNow(2028, 2, 15, 9, 0, 0);
  scene.addEvent(2028, 2, 29, "Leap Day");
  Menu::previewCalendarScreen(2028, 2);
  save("calendar_feb_leap");
}

struct Scene {
  const char *name;
  void (*fn)();
};

const Scene SCENES[] = {
    {"weather_typhoon", sceneWeatherTyphoon},
    {"weather_long_names", sceneWeatherLongNames},
    {"weather_no_city", sceneWeatherNoCity},
    {"weather_no_forecast_yet", sceneWeatherNoForecastYet},
    {"weather_scrolled", sceneWeatherScrolled},
    {"calendar_no_coverage", sceneCalendarAugust2026},
    {"calendar_with_festivals", sceneCalendarWithFestivals},
    {"calendar_no_city", sceneCalendarNoCity},
    {"calendar_feb_leap", sceneCalendarFebLeap},
};
const int SCENE_COUNT = sizeof(SCENES) / sizeof(SCENES[0]);

} // namespace

int main(int argc, char **argv) {
  std::vector<std::string> want;
  for (int i = 1; i < argc; i++) want.push_back(argv[i]);

  int ran = 0;
  for (int i = 0; i < SCENE_COUNT; i++) {
    if (!want.empty()) {
      bool match = false;
      for (auto &w : want) if (w == SCENES[i].name) match = true;
      if (!match) continue;
    }
    SCENES[i].fn();
    ran++;
  }

  if (ran == 0) {
    fprintf(stderr, "No matching scene. Available:\n");
    for (int i = 0; i < SCENE_COUNT; i++) fprintf(stderr, "  %s\n", SCENES[i].name);
    return 1;
  }
  return 0;
}
