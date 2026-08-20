#include "video_player.h"
#include "sd_card.h"

namespace {

const char *VIDEO_PATH = "/video/video.bin";
const size_t HEADER_SIZE = 12; // 4-byte magic + 4x uint16 (LE)

// Sanity cap on frameCount*width*height*2 (the header is trusted data we
// wrote ourselves via the upload endpoint, but a torn/partial write - e.g.
// power lost mid-upload - could leave a header whose frame count implies a
// file far longer than what actually got written; this just bounds how
// large a single frame's read buffer can ever be asked to grow).
const size_t MAX_FRAME_BYTES = 400000; // headroom above 320x140x2 (89600)

bool headerLoaded = false;
bool valid = false;
uint16_t vidW = 0, vidH = 0, frameCount = 0, fps = 0;
size_t frameBytes = 0;

uint16_t *frameBuf = nullptr; // PSRAM, allocated once frameBytes is known
size_t frameBufSize = 0;

int curFrame = 0;
unsigned long nextDue = 0;

uint16_t readU16LE(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

void loadHeaderIfNeeded() {
  if (headerLoaded) return;
  headerLoaded = true;

  uint8_t hdr[HEADER_SIZE];
  if (SdCard::readAt(VIDEO_PATH, 0, hdr, HEADER_SIZE) != HEADER_SIZE) return;
  if (hdr[0] != 'V' || hdr[1] != 'I' || hdr[2] != 'D' || hdr[3] != '1') return;

  uint16_t w = readU16LE(hdr + 4);
  uint16_t h = readU16LE(hdr + 6);
  uint16_t n = readU16LE(hdr + 8);
  uint16_t f = readU16LE(hdr + 10);
  if (w == 0 || h == 0 || n == 0 || f == 0) return;

  size_t bytes = (size_t)w * (size_t)h * 2;
  if (bytes > MAX_FRAME_BYTES) return;

  vidW = w;
  vidH = h;
  frameCount = n;
  fps = f;
  frameBytes = bytes;
  valid = true;
}

} // namespace

namespace VideoPlayer {

bool isAvailable() {
  loadHeaderIfNeeded();
  return valid;
}

void reset() {
  curFrame = 0;
  nextDue = 0; // due immediately on the next draw() call
}

void draw(TFT_eSPI &tft, int x, int y, int w, int h) {
  loadHeaderIfNeeded();
  if (!valid || (int)vidW != w || (int)vidH != h) return;

  unsigned long now = millis();
  if (now < nextDue) return;

  if (!frameBuf || frameBufSize < frameBytes) {
    frameBuf = (uint16_t *)ps_malloc(frameBytes);
    frameBufSize = frameBuf ? frameBytes : 0;
  }
  if (!frameBuf) return;

  size_t offset = HEADER_SIZE + (size_t)curFrame * frameBytes;
  size_t got = SdCard::readAt(VIDEO_PATH, offset, reinterpret_cast<uint8_t *>(frameBuf), frameBytes);
  if (got == frameBytes) {
    // Whatever last drew on this same tft (e.g. showBootMessage()'s smooth-
    // font boot text, before the clock face ever starts) can leave its
    // swap-bytes state set - TFT_eSPI's smooth-font rendering is known to
    // do this (see clock_display.cpp's pushCustomBgSlice() comment, which
    // hit the same symptom: a byte-swapped RGB565 value doesn't just look
    // miscoloured, its 5/6/5 bit fields land on unrelated channels, which
    // reads as scattered speckle/blocks rather than a uniformly wrong
    // colour). Reset it explicitly before every push rather than assuming
    // whatever ran before left it in the state pushImage() expects.
    tft.setSwapBytes(false);
    tft.pushImage(x, y, w, h, frameBuf);
  }

  curFrame = (curFrame + 1) % frameCount;
  nextDue = now + (1000UL / fps);
}

} // namespace VideoPlayer
