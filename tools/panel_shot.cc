// panel_shot.cc — render the panel offscreen and write a PNG.
//
// No display, no SDL, no zlib: draws into a plain RGB565 buffer exactly as the
// GLCDC would scan it out, then writes a PNG using stored (uncompressed)
// deflate blocks.
//
// Builds as the `panel_shot` CMake target (links `spike`), or standalone:
//   g++ -std=c++17 -O1 -Ispike -Iengine -Icontroller \
//       tools/panel_shot.cc spike/*.cc engine/*.cc controller/*.cc \
//       -o panel_shot -lm
//
// Usage:
//   ./panel_shot out.png [scale]
//
// Useful as a golden-image fixture alongside the golden-hash check in
// test_panel.cc: a hash says something changed, an image says what.

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine.h"
#include "panel.h"

namespace {

constexpr int kW = 1024;
constexpr int kH = 600;

// ---- minimal PNG writer (stored deflate; no zlib) ----------------------

std::uint32_t Crc32(const std::uint8_t *d, std::size_t n, std::uint32_t crc = 0) {
  static std::uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  crc = ~crc;
  for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

void Be32(std::vector<std::uint8_t> &v, std::uint32_t x) {
  v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

void Chunk(std::FILE *f, const char *tag, const std::vector<std::uint8_t> &data) {
  std::vector<std::uint8_t> hdr;
  Be32(hdr, static_cast<std::uint32_t>(data.size()));
  std::fwrite(hdr.data(), 1, hdr.size(), f);
  std::fwrite(tag, 1, 4, f);
  if (!data.empty()) std::fwrite(data.data(), 1, data.size(), f);
  std::uint32_t crc = Crc32(reinterpret_cast<const std::uint8_t *>(tag), 4);
  crc = Crc32(data.data(), data.size(), crc);
  std::vector<std::uint8_t> tail;
  Be32(tail, crc);
  std::fwrite(tail.data(), 1, tail.size(), f);
}

// raw = h rows of (1 filter byte + w*3 RGB bytes)
bool WritePng(const char *path, int w, int h, const std::vector<std::uint8_t> &raw) {
  std::FILE *f = std::fopen(path, "wb");
  if (!f) return false;
  static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::fwrite(sig, 1, 8, f);

  std::vector<std::uint8_t> ihdr;
  Be32(ihdr, static_cast<std::uint32_t>(w));
  Be32(ihdr, static_cast<std::uint32_t>(h));
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // colour type: truecolour
  ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  Chunk(f, "IHDR", ihdr);

  // zlib stream: 0x78 0x01, then stored deflate blocks, then adler32.
  std::vector<std::uint8_t> z{0x78, 0x01};
  std::size_t off = 0;
  while (off < raw.size()) {
    const std::size_t n = std::min<std::size_t>(65535, raw.size() - off);
    const bool last = (off + n == raw.size());
    z.push_back(last ? 1 : 0);
    z.push_back(n & 0xFF); z.push_back((n >> 8) & 0xFF);
    z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
    off += n;
  }
  std::uint32_t a = 1, b = 0;
  for (std::uint8_t byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
  Be32(z, (b << 16) | a);
  Chunk(f, "IDAT", z);
  Chunk(f, "IEND", {});
  std::fclose(f);
  return true;
}

// ---- RGB565 -> RGB888 --------------------------------------------------

inline void Expand(std::uint16_t c, std::uint8_t *out) {
  const std::uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  out[0] = static_cast<std::uint8_t>((r * 255 + 15) / 31);
  out[1] = static_cast<std::uint8_t>((g * 255 + 31) / 63);
  out[2] = static_cast<std::uint8_t>((b * 255 + 15) / 31);
}

}  // namespace

int main(int argc, char **argv) {
  const char *path = (argc > 1) ? argv[1] : "panel.png";
  const int scale = (argc > 2) ? std::atoi(argv[2]) : 1;

  engine::EngineInit();
  spike::Panel *p = spike::PanelCreate();

  std::vector<std::uint16_t> buf0(kW * kH), buf1(kW * kH);
  spike::FrameBuffer fb0{buf0.data(), kW, kH, kW, spike::Rect{0, 0, kW, kH}};
  spike::FrameBuffer fb1{buf1.data(), kW, kH, kW, spike::Rect{0, 0, kW, kH}};

  // Two draws fill both buffers with chrome; extra pairs settle the plots.
  spike::PanelDraw(p, fb0, 0);
  spike::PanelDraw(p, fb1, 1);
  spike::PanelNoteOn(p, 440.0f, 127);
  for (int i = 0; i < 3; ++i) {
    spike::PanelDraw(p, fb0, 0);
    spike::PanelDraw(p, fb1, 1);
  }

  const int ow = kW * scale, oh = kH * scale;
  std::vector<std::uint8_t> raw;
  raw.reserve(static_cast<std::size_t>(oh) * (1 + ow * 3));
  std::uint8_t rgb[3];
  for (int y = 0; y < oh; ++y) {
    raw.push_back(0);  // filter type: none
    const std::uint16_t *row = buf0.data() + static_cast<std::size_t>(y / scale) * kW;
    for (int x = 0; x < ow; ++x) {
      Expand(row[x / scale], rgb);
      raw.insert(raw.end(), rgb, rgb + 3);
    }
  }

  if (!WritePng(path, ow, oh, raw)) {
    std::fprintf(stderr, "panel_shot: cannot write %s\n", path);
    return 1;
  }
  std::printf("panel_shot: wrote %s (%dx%d)\n", path, ow, oh);
  return 0;
}
