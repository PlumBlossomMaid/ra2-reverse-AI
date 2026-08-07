#include "mix_archive.h"

#include <cctype>
#include <cstring>

#include "bignum.h"
#include "blowfish.h"

namespace ra2 {
namespace mix {
namespace {

std::uint32_t Crc32(const std::uint8_t* p, std::size_t n) {
  static std::uint32_t table[256];
  static bool built = false;
  if (!built) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
      table[i] = c;
    }
    built = true;
  }
  std::uint32_t c = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

// TS/RA2 的名字预处理：大写，再按 4 字节对齐补齐。补法本身很怪
//（末位先填「已凑满的字节数」，再拿最后一个完整 4 字节组的首字节顶满），
// 但原版就是这么算的，照抄。
std::vector<std::uint8_t> PadName(const std::string& name) {
  std::vector<std::uint8_t> s;
  s.reserve(name.size() + 4);
  for (std::size_t i = 0; i < name.size(); ++i) {
    s.push_back(static_cast<std::uint8_t>(
        std::toupper(static_cast<unsigned char>(name[i]))));
  }
  const std::size_t n = s.size();
  const std::size_t a = n >> 2;
  if (n & 3) {
    s.push_back(static_cast<std::uint8_t>(n - (a << 2)));
    for (std::size_t k = 0; k < 3 - (n & 3); ++k) s.push_back(s[a << 2]);
  }
  return s;
}

// Westwood 的公钥（社区通行的那一串 base64 blob）。
// 结构：0x02, len, <len 字节模数（大端）>。指数固定 65537。
const char kPubKeyB64[] =
    "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V";

int B64Val(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

std::vector<std::uint8_t> B64Decode(const char* s) {
  std::vector<std::uint8_t> out;
  std::uint32_t acc = 0;
  int bits = 0;
  for (const char* p = s; *p; ++p) {
    const int v = B64Val(*p);
    if (v < 0) continue;
    acc = (acc << 6) | static_cast<std::uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<std::uint8_t>(acc >> bits));
    }
  }
  return out;
}

std::uint16_t RdU16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t RdU32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

std::uint32_t MixId(const std::string& name) {
  const std::vector<std::uint8_t> p = PadName(name);
  return Crc32(p.empty() ? nullptr : &p[0], p.size());
}

std::vector<std::uint8_t> BlowfishKeyFromSource(const std::uint8_t* src80) {
  const std::vector<std::uint8_t> blob = B64Decode(kPubKeyB64);
  std::vector<std::uint8_t> key;
  if (blob.size() < 2 || blob[0] != 0x02) return key;
  const std::size_t len = blob[1];
  if (blob.size() < 2 + len) return key;
  // ⚠️ 模数按大端读。小端读出来只有 317 位，且首个密文块就 >= n，解不动。
  const BigUint n = BigUint::FromBytesBE(&blob[2], len);
  const int blk = (n.BitLength() - 1) / 8;      // 319 位 -> 39 字节明文块
  std::vector<std::uint8_t> plain;
  for (int i = 0; i < 80; i += 40) {
    const BigUint c = BigUint::FromBytesLE(src80 + i, 40);   // 密文块小端
    const BigUint m = c.ModPow(65537u, n);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(blk), 0);
    m.ToBytesLE(&buf[0], buf.size());
    plain.insert(plain.end(), buf.begin(), buf.end());
  }
  if (plain.size() < 56) return key;
  key.assign(plain.begin(), plain.begin() + 56);
  return key;
}

bool MixArchive::ParseHeader() {
  if (data_.size() < 4) return false;
  const std::uint32_t flags = RdU32(&data_[0]);
  std::size_t off = 0;
  if (flags & 0xFFFFu) {
    off = 0;                                   // 老版头：直接是 count/data_size
    encrypted_ = false;
    checksum_ = false;
  } else {
    off = 4;
    encrypted_ = (flags & 0x00020000u) != 0;
    checksum_ = (flags & 0x00010000u) != 0;
  }

  std::vector<std::uint8_t> hdr;
  const std::uint8_t* hp = nullptr;
  std::size_t hn = 0;
  if (encrypted_) {
    if (data_.size() < off + 80 + 8) return false;
    const std::vector<std::uint8_t> key = BlowfishKeyFromSource(&data_[off]);
    if (key.size() != 56) return false;
    const Blowfish bf(&key[0], key.size());
    // 先解一块拿到 count，再按需解够目录长度（整个 mix 有 29 MB，
    // 不能整段解）。头是 6 + count*12 字节，向上取整到 8。
    const std::size_t enc_off = off + 80;
    std::vector<std::uint8_t> first(8, 0);
    bf.DecryptEcb(&data_[enc_off], &first[0], 8);
    const std::size_t count = RdU16(&first[0]);
    const std::size_t need = ((6 + count * 12 + 7) / 8) * 8;
    if (data_.size() < enc_off + need) return false;
    hdr.assign(need, 0);
    bf.DecryptEcb(&data_[enc_off], &hdr[0], need);
    hp = &hdr[0];
    hn = need;
    base_ = enc_off + need;
  } else {
    if (data_.size() < off + 6) return false;
    hp = &data_[off];
    hn = data_.size() - off;
    base_ = 0;   // 下面按 count 定
  }

  const std::size_t count = RdU16(hp);
  if (hn < 6 + count * 12) return false;
  if (!encrypted_) base_ = off + 6 + count * 12;
  entries_.clear();
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint8_t* e = hp + 6 + i * 12;
    Entry en;
    en.offset = RdU32(e + 4);
    en.size = RdU32(e + 8);
    entries_[RdU32(e)] = en;
  }
  return true;
}

bool MixArchive::Open(std::vector<std::uint8_t> data) {
  data_.swap(data);
  return ParseHeader();
}

bool MixArchive::OpenNested(const MixArchive& parent, const std::string& name) {
  std::size_t sz = 0;
  const std::uint8_t* p = parent.Find(name, &sz);
  if (p == nullptr) return false;
  std::vector<std::uint8_t> buf(p, p + sz);
  return Open(std::move(buf));
}

const std::uint8_t* MixArchive::Find(const std::string& name,
                                     std::size_t* size) const {
  const std::map<std::uint32_t, Entry>::const_iterator it =
      entries_.find(MixId(name));
  if (it == entries_.end()) return nullptr;
  const std::size_t a = base_ + it->second.offset;
  const std::size_t b = a + it->second.size;
  if (b > data_.size() || a > b) return nullptr;
  if (size != nullptr) *size = it->second.size;
  return &data_[a];
}

bool MixArchive::Read(const std::string& name,
                      std::vector<std::uint8_t>* out) const {
  std::size_t sz = 0;
  const std::uint8_t* p = Find(name, &sz);
  if (p == nullptr) return false;
  out->assign(p, p + sz);
  return true;
}

}  // namespace mix
}  // namespace ra2
