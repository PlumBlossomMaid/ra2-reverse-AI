#include "blowfish.h"

#include <cstring>

#include "blowfish_tables.h"

namespace ra2 {
namespace mix {

Blowfish::Blowfish(const std::uint8_t* key, std::size_t key_len) {
  std::memcpy(p_, kBlowfishP, sizeof(p_));
  std::memcpy(s_, kBlowfishS, sizeof(s_));
  if (key_len == 0) key_len = 1;

  std::size_t k = 0;
  for (int i = 0; i < 18; ++i) {
    std::uint32_t d = 0;
    for (int b = 0; b < 4; ++b) {
      d = (d << 8) | key[k];
      k = (k + 1) % key_len;
    }
    p_[i] ^= d;
  }

  std::uint32_t l = 0, r = 0;
  for (int i = 0; i < 18; i += 2) {
    EncryptBlock(&l, &r);
    p_[i] = l;
    p_[i + 1] = r;
  }
  for (int box = 0; box < 4; ++box) {
    for (int i = 0; i < 256; i += 2) {
      EncryptBlock(&l, &r);
      s_[box][i] = l;
      s_[box][i + 1] = r;
    }
  }
}

std::uint32_t Blowfish::F(std::uint32_t x) const {
  const std::uint32_t a = s_[0][(x >> 24) & 0xFF];
  const std::uint32_t b = s_[1][(x >> 16) & 0xFF];
  const std::uint32_t c = s_[2][(x >> 8) & 0xFF];
  const std::uint32_t d = s_[3][x & 0xFF];
  return (((a + b) ^ c) + d) & 0xFFFFFFFFu;
}

void Blowfish::EncryptBlock(std::uint32_t* xl, std::uint32_t* xr) const {
  std::uint32_t l = *xl, r = *xr;
  for (int i = 0; i < 16; ++i) {
    l ^= p_[i];
    r ^= F(l);
    const std::uint32_t t = l;
    l = r;
    r = t;
  }
  const std::uint32_t t = l;
  l = r;
  r = t;
  r ^= p_[16];
  l ^= p_[17];
  *xl = l;
  *xr = r;
}

void Blowfish::DecryptBlock(std::uint32_t* xl, std::uint32_t* xr) const {
  std::uint32_t l = *xl, r = *xr;
  for (int i = 17; i > 1; --i) {
    l ^= p_[i];
    r ^= F(l);
    const std::uint32_t t = l;
    l = r;
    r = t;
  }
  const std::uint32_t t = l;
  l = r;
  r = t;
  r ^= p_[1];
  l ^= p_[0];
  *xl = l;
  *xr = r;
}

void Blowfish::DecryptEcb(const std::uint8_t* src, std::uint8_t* dst,
                          std::size_t len) const {
  for (std::size_t off = 0; off + 8 <= len; off += 8) {
    std::uint32_t l = (static_cast<std::uint32_t>(src[off]) << 24) |
                      (static_cast<std::uint32_t>(src[off + 1]) << 16) |
                      (static_cast<std::uint32_t>(src[off + 2]) << 8) |
                      static_cast<std::uint32_t>(src[off + 3]);
    std::uint32_t r = (static_cast<std::uint32_t>(src[off + 4]) << 24) |
                      (static_cast<std::uint32_t>(src[off + 5]) << 16) |
                      (static_cast<std::uint32_t>(src[off + 6]) << 8) |
                      static_cast<std::uint32_t>(src[off + 7]);
    DecryptBlock(&l, &r);
    dst[off] = static_cast<std::uint8_t>(l >> 24);
    dst[off + 1] = static_cast<std::uint8_t>(l >> 16);
    dst[off + 2] = static_cast<std::uint8_t>(l >> 8);
    dst[off + 3] = static_cast<std::uint8_t>(l);
    dst[off + 4] = static_cast<std::uint8_t>(r >> 24);
    dst[off + 5] = static_cast<std::uint8_t>(r >> 16);
    dst[off + 6] = static_cast<std::uint8_t>(r >> 8);
    dst[off + 7] = static_cast<std::uint8_t>(r);
  }
}

}  // namespace mix
}  // namespace ra2
