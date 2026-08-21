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
File writeFile;
File seqReadFile;

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

size_t readAt(const String &path, size_t offset, uint8_t *out, size_t len) {
  if (!present) return 0;
  File f = SD.open(path);
  if (!f) return 0;
  if (!f.seek(offset)) {
    f.close();
    return 0;
  }
  size_t got = f.read(out, len);
  f.close();
  return got;
}

bool openSeqRead(const String &path) {
  if (!present) return false;
  if (seqReadFile) seqReadFile.close();
  seqReadFile = SD.open(path);
  return (bool)seqReadFile;
}

size_t readSeqAt(size_t offset, uint8_t *out, size_t len) {
  if (!seqReadFile) return 0;
  if (!seqReadFile.seek(offset)) return 0;
  return seqReadFile.read(out, len);
}

void closeSeqRead() {
  if (seqReadFile) seqReadFile.close();
}

bool exists(const String &path) {
  if (!present) return false;
  return SD.exists(path);
}

bool remove(const String &path) {
  if (!present) return true; // no card - nothing to remove, not an error
  if (!SD.exists(path)) return true;
  return SD.remove(path);
}

bool beginWrite(const String &path) {
  if (!present || writeFile) return false;
  int slash = path.lastIndexOf('/');
  if (slash > 0) {
    String dir = path.substring(0, slash);
    if (!SD.exists(dir)) SD.mkdir(dir);
  }
  // FILE_WRITE's truncate-vs-append behaviour has varied across core
  // versions - remove any existing file first so this is always a clean
  // truncate regardless.
  if (SD.exists(path)) SD.remove(path);
  writeFile = SD.open(path, FILE_WRITE);
  return (bool)writeFile;
}

bool writeChunk(const uint8_t *data, size_t len) {
  if (!writeFile) return false;
  return writeFile.write(data, len) == len;
}

void endWrite() {
  if (writeFile) writeFile.close();
}

} // namespace SdCard
