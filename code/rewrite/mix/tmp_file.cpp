#include "tmp_file.h"

namespace ra2 {
namespace mix {
namespace {

std::uint32_t RdU32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

bool ParseTmp(const std::uint8_t* data, std::size_t size, TmpTile* out) {
  if (data == nullptr || size < 0x10) return false;
  const std::uint32_t bx = RdU32(data);
  const std::uint32_t by = RdU32(data + 4);
  // 偏移表的**项数**用的是完整的 dword（原版就是 `BlocksX * BlocksY` 相乘），
  // 而写进 TileTypeClass 的宽高只取低字节——真实数据里两者一致，
  // 但别把这两处混成一个量。
  const std::uint64_t n = static_cast<std::uint64_t>(bx) * by;
  if (n == 0 || n > 4096) return false;
  if (size < 0x10 + n * 4) return false;

  out->blocks_x = static_cast<int>(bx & 0xFFu);
  out->blocks_y = static_cast<int>(by & 0xFFu);
  out->pixel_w = static_cast<int>(RdU32(data + 8));
  out->pixel_h = static_cast<int>(RdU32(data + 12));
  out->sub.assign(static_cast<std::size_t>(n), TmpSubTile());
  for (std::uint64_t i = 0; i < n; ++i) {
    const std::uint32_t off = RdU32(data + 0x10 + i * 4);
    TmpSubTile& s = out->sub[static_cast<std::size_t>(i)];
    if (off == 0) continue;                       // 空洞
    if (off + 0x2B > size) return false;
    s.present = true;
    s.height = data[off + 0x28];
    s.terrain = static_cast<std::int8_t>(data[off + 0x29]);
    s.ramp = data[off + 0x2A];
  }
  return true;
}

}  // namespace mix
}  // namespace ra2
