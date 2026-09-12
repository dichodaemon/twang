#include "png.h"

#include <algorithm>
#include <cstdio>

namespace spike {

namespace {

std::uint32_t Crc32(const std::uint8_t *d, std::size_t n,
                    std::uint32_t crc = 0) {
  static std::uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k)
        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  crc = ~crc;
  for (std::size_t i = 0; i < n; ++i)
    crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

void Be32(std::vector<std::uint8_t> &v, std::uint32_t x) {
  v.push_back(x >> 24);
  v.push_back(x >> 16);
  v.push_back(x >> 8);
  v.push_back(x);
}

void Chunk(std::FILE *f, const char *tag, const std::vector<std::uint8_t> &d) {
  std::vector<std::uint8_t> h;
  Be32(h, static_cast<std::uint32_t>(d.size()));
  std::fwrite(h.data(), 1, h.size(), f);
  std::fwrite(tag, 1, 4, f);
  if (!d.empty()) std::fwrite(d.data(), 1, d.size(), f);
  std::uint32_t c = Crc32(reinterpret_cast<const std::uint8_t *>(tag), 4);
  c = Crc32(d.data(), d.size(), c);
  std::vector<std::uint8_t> t;
  Be32(t, c);
  std::fwrite(t.data(), 1, t.size(), f);
}

}  // namespace

bool WritePng(const char *path, int w, int h,
              const std::vector<std::uint8_t> &raw) {
  std::FILE *f = std::fopen(path, "wb");
  if (!f) return false;
  static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D,
                                      0x0A, 0x1A, 0x0A};
  std::fwrite(sig, 1, 8, f);

  std::vector<std::uint8_t> ihdr;
  Be32(ihdr, static_cast<std::uint32_t>(w));
  Be32(ihdr, static_cast<std::uint32_t>(h));
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(2);  // colour type: truecolour
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  Chunk(f, "IHDR", ihdr);

  // zlib stream: 0x78 0x01, then stored deflate blocks, then adler32.
  std::vector<std::uint8_t> z{0x78, 0x01};
  std::size_t off = 0;
  while (off < raw.size()) {
    const std::size_t n = std::min<std::size_t>(65535, raw.size() - off);
    const bool last = (off + n == raw.size());
    z.push_back(last ? 1 : 0);
    z.push_back(n & 0xFF);
    z.push_back((n >> 8) & 0xFF);
    z.push_back(~n & 0xFF);
    z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
    off += n;
  }
  std::uint32_t a = 1, b = 0;
  for (std::uint8_t by : raw) {
    a = (a + by) % 65521;
    b = (b + a) % 65521;
  }
  Be32(z, (b << 16) | a);
  Chunk(f, "IDAT", z);
  Chunk(f, "IEND", {});
  std::fclose(f);
  return true;
}

bool WriteFramePng(const FrameBuffer &fb, const char *path, int scale) {
  const int ow = fb.w * scale, oh = fb.h * scale;
  std::vector<std::uint8_t> raw;
  raw.reserve(static_cast<std::size_t>(oh) * (1 + ow * 3));
  for (int y = 0; y < oh; ++y) {
    raw.push_back(0);  // filter: none
    const std::uint16_t *row =
        fb.px + static_cast<std::size_t>(y / scale) * fb.stride;
    for (int x = 0; x < ow; ++x) {
      const std::uint16_t c = row[x / scale];
      const int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
      raw.push_back(static_cast<std::uint8_t>((r * 255 + 15) / 31));
      raw.push_back(static_cast<std::uint8_t>((g * 255 + 31) / 63));
      raw.push_back(static_cast<std::uint8_t>((b * 255 + 15) / 31));
    }
  }
  return WritePng(path, ow, oh, raw);
}

}  // namespace spike
