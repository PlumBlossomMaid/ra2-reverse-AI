#include "cell_ref.h"

#include <cstdint>

namespace ra2 {
namespace rmg {

// 0x89F688：文件里是零（落在 .data 的 BSS 部分），由 0x49F2F0 在运行期
// 逐条填出——下表就是那段机器码的结果。
const CellCoord kAdjacentDelta[8] = {
    {0, -1},   // 0 N
    {1, -1},   // 1 NE
    {1, 0},    // 2 E
    {1, 1},    // 3 SE
    {0, 1},    // 4 S
    {-1, 1},   // 5 SW
    {-1, 0},   // 6 W
    {-1, -1},  // 7 NW
};

CellRef RefOf(const CellCoord& c) { return CellRef{false, c}; }

// 0x5657A0：下标 = (int16 Y << 9) + int16 X；负、>= 0x40000、或表里是空指针
// 都退到哑元，并把坐标写进 0xABDC74。
CellRef Lookup(SmoothWorld& w, int x, int y) {
  const CellCoord c{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)};
  const int index = (c.y << 9) + c.x;
  if (index < 0 || index >= 0x40000 || !w.CellExists(c)) {
    w.SetDummyCoord(c);
    return CellRef{true, c};
  }
  return CellRef{false, c};
}

// 0x481810 AdjacentCell(dir)：dir >= 8 原样返回自己；否则 16 位坐标加法后查表。
CellRef Adjacent(SmoothWorld& w, const CellRef& r, int dir) {
  if (static_cast<unsigned>(dir) >= 8u) return r;
  const CellCoord base = CoordOf(w, r);
  const CellCoord& d = kAdjacentDelta[dir & 7];
  return Lookup(w, base.x + d.x, base.y + d.y);
}

CellCoord CoordOf(const SmoothWorld& w, const CellRef& r) {
  return r.dummy ? w.DummyCoord() : r.coord;
}

int TileOf(const SmoothWorld& w, const CellRef& r) {
  return r.dummy ? w.DummyIsoTile() : w.Tile(r.coord);
}

}  // namespace rmg
}  // namespace ra2
