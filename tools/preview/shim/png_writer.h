#pragma once
// A PNG writer with no dependencies - deflate "stored" (uncompressed)
// blocks, which is a valid zlib stream. The files are bigger than a real
// encoder would produce, but a 320x170 screenshot is ~160KB either way
// and it saves the harness from needing libpng or zlib installed.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace png {

inline uint32_t crc32(const uint8_t *data, size_t len, uint32_t crc = 0) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  crc = crc ^ 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

inline void be32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back((v >> 24) & 0xFF);
  out.push_back((v >> 16) & 0xFF);
  out.push_back((v >> 8) & 0xFF);
  out.push_back(v & 0xFF);
}

inline void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data) {
  be32(out, (uint32_t)data.size());
  std::vector<uint8_t> body(type, type + 4);
  body.insert(body.end(), data.begin(), data.end());
  out.insert(out.end(), body.begin(), body.end());
  be32(out, crc32(body.data(), body.size()));
}

// Writes an RGB565 framebuffer as a 24-bit RGB PNG, optionally scaled up
// by an integer factor so individual pixels stay visible when a 320x170
// panel is viewed on a desktop screen.
inline bool writeRgb565(const std::string &path, const uint16_t *fb, int w, int h, int scale = 1) {
  if (scale < 1) scale = 1;
  const int ow = w * scale, oh = h * scale;

  std::vector<uint8_t> raw;
  raw.reserve((size_t)oh * (ow * 3 + 1));
  for (int y = 0; y < oh; y++) {
    raw.push_back(0); // filter: none
    for (int x = 0; x < ow; x++) {
      uint16_t p = fb[(size_t)(y / scale) * w + (x / scale)];
      uint8_t r = (uint8_t)((p >> 11) & 0x1F), g = (uint8_t)((p >> 5) & 0x3F), b = (uint8_t)(p & 0x1F);
      // Replicate the high bits downward so 0x1F maps to 0xFF, matching
      // how the panel drives a full-brightness channel.
      raw.push_back((uint8_t)((r << 3) | (r >> 2)));
      raw.push_back((uint8_t)((g << 2) | (g >> 4)));
      raw.push_back((uint8_t)((b << 3) | (b >> 2)));
    }
  }

  // zlib stream: 0x78 0x01, then stored deflate blocks, then adler32.
  std::vector<uint8_t> z;
  z.push_back(0x78);
  z.push_back(0x01);
  const size_t MAXBLK = 65535;
  for (size_t off = 0; off < raw.size(); off += MAXBLK) {
    size_t n = raw.size() - off < MAXBLK ? raw.size() - off : MAXBLK;
    z.push_back(off + n >= raw.size() ? 1 : 0);
    z.push_back(n & 0xFF);
    z.push_back((n >> 8) & 0xFF);
    z.push_back(~n & 0xFF);
    z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
  }
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
  be32(z, (b << 16) | a);

  std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  be32(ihdr, (uint32_t)ow);
  be32(ihdr, (uint32_t)oh);
  ihdr.push_back(8); // bit depth
  ihdr.push_back(2); // colour type: truecolour
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", {});

  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
  fclose(f);
  return ok;
}

} // namespace png
