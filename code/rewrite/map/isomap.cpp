// IsoMapPack5 编码实现 —— 见 isomap.h 头部注释。
#include "isomap.h"

#include <algorithm>
#include <cstring>

#include "base64.h"
#include "lzo.h"

namespace mapenc {

namespace {
constexpr size_t kPipeBlock = 0x2000;
constexpr uint32_t kInvalidIsoTile = 0xFFFF;

void AppendU16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(v & 0xFF);
  out.push_back((v >> 8) & 0xFF);
}
}  // namespace

std::vector<uint8_t> LzoPipeCompress(const std::vector<uint8_t>& raw) {
  std::vector<uint8_t> out;
  out.reserve(raw.size() + raw.size() / kPipeBlock * 4 + 4);
  size_t pos = 0;
  while (pos < raw.size()) {
    const size_t n = std::min(kPipeBlock, raw.size() - pos);
    const std::vector<uint8_t> comp = LzoCompress(raw.data() + pos, n);
    AppendU16(out, static_cast<uint16_t>(comp.size()));
    AppendU16(out, static_cast<uint16_t>(n));
    out.insert(out.end(), comp.begin(), comp.end());
    pos += n;
  }
  return out;
}

std::vector<std::string> UUBlockLines(const std::vector<uint8_t>& data) {
  const std::string b64 = Base64Encode(data);
  std::vector<std::string> lines;
  for (size_t i = 0; i < b64.size(); i += 70) {
    lines.push_back(b64.substr(i, 70));
  }
  return lines;
}

std::vector<uint8_t> IsoMapPack5Encode(const std::vector<const IsoCell*>& slots,
                                       uint32_t vector_max, int w, int h) {
  std::vector<uint8_t> records;
  records.reserve(16 * 11 + 4);
  const int w2h = w + 2 * h;
  const uint32_t n = std::min<uint32_t>(vector_max,
                                        static_cast<uint32_t>(slots.size()));
  for (uint32_t i = 1; i < n; ++i) {  // i==0 槽跳过（0x56B3F0 语义）
    const IsoCell* c = slots[i];
    if (c == nullptr) continue;
    const int X = c->x;
    const int Y = c->y;
    // InMap'：X+Y > W && X-Y < W && Y-X < W && X+Y <= W+2H
    if (!(X + Y > w && X - Y < w && Y - X < w && X + Y <= w2h)) continue;
    // coord != 默认 0（(Y<<16)|X == 0 当且仅当 X==0 && Y==0）
    if (X == 0 && Y == 0) continue;
    // iso != 0xFFFF || level > 0（InvalidCell 由调用方以 NULL 槽表达）
    if (c->isoTile == kInvalidIsoTile && c->level == 0) continue;
    const uint32_t packed = (static_cast<uint32_t>(Y) << 16) | X;
    records.push_back(packed & 0xFF);
    records.push_back((packed >> 8) & 0xFF);
    records.push_back((packed >> 16) & 0xFF);
    records.push_back((packed >> 24) & 0xFF);
    records.push_back(c->isoTile & 0xFF);
    records.push_back((c->isoTile >> 8) & 0xFF);
    records.push_back((c->isoTile >> 16) & 0xFF);
    records.push_back((c->isoTile >> 24) & 0xFF);
    records.push_back(c->height);
    records.push_back(c->level);
    records.push_back(c->unknown);
  }
  // 哨兵 = DAT_00abd480（InvalidCell 坐标 = 0x00000000，dump_isomap_golden
  // oracle 实测）。游戏写链 0x56B3F0 末尾 `local_2c = DAT_00abd480`；
  // 读链遇坐标 == 0 即结束（0x56B3F0 对应解）。故 (0,0) 格永不写入。
  records.insert(records.end(), 4, 0);
  return LzoPipeCompress(records);
}

}  // namespace mapenc
