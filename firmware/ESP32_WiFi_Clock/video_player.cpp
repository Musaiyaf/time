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

// Frame data is staged into frameBuf above, then copied pixel-by-pixel
// into this sprite and pushed via TFT_eSprite::pushSprite() - deliberately
// NOT tft.pushImage(frameBuf) directly, even though frameBuf already holds
// correctly-ordered RGB565 data. This codebase hit that exact failure
// mode once before, in Custom Face's background rendering (see
// clock_display.cpp's pushCustomBgSlice() comment): pushImage() of a
// large raw buffer produced scrambled "TV static" colours even with
// swap-bytes reset first, for reasons that were never fully pinned down -
// only going through a sprite's drawPixel()/pushSprite() (proven correct;
// every other face already does exactly this for its own content) turned
// out to sidestep it entirely. Lazily created because it needs a TFT_eSPI*
// (only available once draw() is first called, not at static-init time)
// and, like every other sprite in this firmware, never freed afterwards.
TFT_eSprite *frameSpr = nullptr;
int frameSprW = 0, frameSprH = 0;

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

bool matchesSize(int w, int h) {
  loadHeaderIfNeeded();
  return valid && (int)vidW == w && (int)vidH == h;
}

void reset() {
  curFrame = 0;
  nextDue = 0; // due immediately on the next draw() call
}

void invalidate() {
  headerLoaded = false;
  valid = false;
  reset();
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

  if (!frameSpr) frameSpr = new TFT_eSprite(&tft);
  if (frameSprW != w || frameSprH != h) {
    frameSpr->setColorDepth(16);
    frameSpr->createSprite(w, h);
    frameSprW = w;
    frameSprH = h;
  }

  size_t offset = HEADER_SIZE + (size_t)curFrame * frameBytes;
  size_t got = SdCard::readAt(VIDEO_PATH, offset, reinterpret_cast<uint8_t *>(frameBuf), frameBytes);
  if (got == frameBytes) {
    for (int py = 0; py < h; py++) {
      const uint16_t *row = frameBuf + (size_t)py * w;
      for (int px = 0; px < w; px++) {
        frameSpr->drawPixel(px, py, row[px]);
      }
    }
    frameSpr->pushSprite(x, y);
  }

  curFrame = (curFrame + 1) % frameCount;
  nextDue = now + (1000UL / fps);
}

} // namespace VideoPlayer
