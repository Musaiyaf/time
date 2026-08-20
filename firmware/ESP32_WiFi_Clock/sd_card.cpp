#include "sd_card.h"
#include "config.h"
#include <SPI.h>
#include <SD.h>

namespace {
// A dedicated SPI bus (HSPI) for the card, separate from whatever bus
// TFT_eSPI drives the display on - avoids any risk of the two libraries
// fighting over shared bus state.
SPIClass sdSPI(HSPI);
bool present = false;

// entry.name() has returned either a full path or just a base name
// depending on core version - normalize to just the base name so paths
// built from it (see menu.cpp's runSdBrowser()) are always predictable.
String baseName(const String &rawName) {
  int slash = rawName.lastIndexOf('/');
  return (slash >= 0) ? rawName.substring(slash + 1) : rawName;
}
} // namespace

namespace SdCard {

void begin() {
  sdSPI.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  present = SD.begin(SD_CS_PIN, sdSPI);
}

bool isPresent() { return present; }

int listDir(const String &path, Entry *out, int maxEntries) {
  if (!present) return 0;

  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return 0;
  }

  int count = 0;
  File entry = dir.openNextFile();
  while (entry && count < maxEntries) {
    out[count].name = baseName(entry.name());
    out[count].isDir = entry.isDirectory();
    out[count].size = entry.isDirectory() ? 0 : entry.size();
    count++;
    entry.close();
    entry = dir.openNextFile();
  }
  if (entry) entry.close();
  dir.close();
  return count;
}

} // namespace SdCard
