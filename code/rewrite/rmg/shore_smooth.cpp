#include "shore_smooth.h"

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "cell_ref.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {

// 0xABDB64（4 字节步长的两个 int16），由 CRT 静态初始化器 0x57A9C0 填出。
const short kShoreOffsetX[43] = {
   0,  0,  0,  0,  0,  0, -1, -1, -1, -1, -1, -1, -1, -2, -2, -1,
  -1,  0,  0,  0,  0,  0, -1,  0,  0,  0,  0,  0,  0,  0,  0,  0,
   0, -1, -1, -1, -1,  0,  0,  0,  0,  0,  0,
};
const short kShoreOffsetY[43] = {
   0, -1, -1, -1, -1, -2, -2, -1, -1,  0,  0,  0,  0, -1,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, -1,  0, -1,
  -1, -1, -1,  0,  0,  0,  0, -1, -1,  0,  0,
};

namespace {

bool IsWaterRef(const SmoothWorld& w, const TheaterData& theater,
                const CellRef& r) {
  return IsWaterTile(TileOf(w, r), theater.water_set);   // 0x485060
}

// ── 第 1 遍：侵蚀（0x57A430，ret 0xC）────────────────────────────────
// 返回 false = 撞上了别的陆块，整块作废。
bool Pass1Erode(SmoothWorld& w, const MapGrid& grid,
                const TheaterData& theater, const CellCoord& cell,
                int blob_id, bool flag) {
  // 0x57A451-0x57A46F：先用 mode 2 算一遍写进缓存，再用 mode 0 查一遍。
  // 第二次必然命中刚写的缓存（缓存不区分 mode），所以 mask == m2。
  // 这不是冗余——它让 mode 0 那道「中心格必须未定砖」的门槛被绕了过去。
  const int m2 = ShoreMask(w, grid, theater, cell, 2);
  w.SetMaskCache(cell, m2);
  const int mask = ShoreMask(w, grid, theater, cell, 0);

  if (mask <= 0) return true;                       // 0x57A477 有符号 jle

  bool erode = false;
  if (mask == 0x0B || mask == 0x1A) erode = true;   // 0x57A47D

  // 0x57A48C-0x57A4CD：四组「两个正方向是水、而它们夹的那对对角是陆」
  if (((mask & 0xA0) == 0xA0 && (mask & 0x11) == 0) ||
      ((mask & 0x82) == 0x82 && (mask & 0x44) == 0) ||
      ((mask & 0x0A) == 0x0A && (mask & 0x11) == 0) ||
      ((mask & 0x28) == 0x28 && (mask & 0x44) == 0)) {
    erode = true;
  }

  // 0x57A4D2-0x57A7CF：四段「隔 1 格 / 隔 2 格的对侧也是水」探测。
  // 两个探针都会求值（原版没有短路），而 Lookup 会改写共享哑元的坐标，
  // 所以求值顺序不能动；四段之间同样不短路。
  struct Probe { int trigger; int dx; int dy; int want; };
  static const Probe kProbes[4] = {
      {0x20, +1, 0, 0x02},   // 西边是水 → 往东探对侧的水
      {0x02, -1, 0, 0x20},   // 东边是水 → 往西探
      {0x08, 0, -1, 0x80},   // 南边是水 → 往北探
      {0x80, 0, +1, 0x08},   // 北边是水 → 往南探
  };
  for (const Probe& p : kProbes) {
    if (!(mask & p.trigger)) continue;
    const CellRef a = Lookup(w, cell.x + p.dx, cell.y + p.dy);
    const int ma = ShoreMask(w, grid, theater, CoordOf(w, a), 0);
    const CellRef b = Lookup(w, cell.x + 2 * p.dx, cell.y + 2 * p.dy);
    const int mb = ShoreMask(w, grid, theater, CoordOf(w, b), 0);
    if ((ma & p.want) || (mb & p.want)) erode = true;
  }

  // 0x57A7D3：一格宽的横向 / 纵向地峡
  if ((mask & 0x88) == 0x88 || (mask & 0x22) == 0x22) erode = true;
  if (!erode) return true;

  // 0x57A7F4：撞上别的块就整块作废（flag 非零时不作废）
  const int owner = w.WorkBlob(cell);
  if (owner > 0 && owner != blob_id && !flag) return false;

  // 0x57A82A：退回水
  w.SetSubTile(cell, 0);                       // +0x11A
  w.SetIsoTile(cell, theater.water_set);       // +0x38 = [0xAA0738]
  w.SetWorkBlob(cell, flag ? 0 : blob_id);     // 0x5A0090

  // 0x57A86A：八邻格——在界内就让缓存失效，然后递归。
  for (int d = 0; d < 8; ++d) {
    const CellRef n = Adjacent(w, RefOf(cell), d);
    const CellCoord nc = CoordOf(w, n);
    if (!grid.InMap(nc)) continue;
    w.SetMaskCache(nc, -1);
    // 递归的返回值被丢弃（0x57A8D5 之后 0x57A8DA 立刻 mov eax,[esp+0x2c]）。
    // 于是「撞上别的块」只有最外层那次调用能上报，内层一律咽下。
    Pass1Erode(w, grid, theater, nc, blob_id, flag);
  }
  return true;                                 // 0x57A8E8 恒为真
}

// ── 第 2 遍：清理（0x57A320，ret 0xC，返回值被编排丢弃）──────────────
void Pass2Cleanup(SmoothWorld& w, const MapGrid& grid,
                  const TheaterData& theater, const CellCoord& cell,
                  int blob_id, bool flag) {
  // 0x57A330：判水区间宽度是 0x0C，不是别处那个 0x0E。照抄。
  const int tile = w.Tile(cell);
  const int water = theater.water_set;
  if (tile < water || tile >= water + 0x0C) return;

  const int mask = ShoreMask(w, grid, theater, cell, 2);
  switch (mask) {                              // 0x57A34E 起八个字面量
    case 0xC7: case 0x7C: case 0xF1: case 0x1F:
    case 0xC6: case 0x6C: case 0xB1: case 0x1B:
      break;
    default:
      return;
  }

  w.SetIsoTile(cell, 0xFFFF);                  // 0x57A38D
  w.SetSubTile(cell, 0);                       // 0x57A394
  if (flag) w.SetWorkBlob(cell, blob_id);      // 0x57A39C

  // 0x57A3AB：八邻格「先置 -1 再现算再写回」——正好绕开缓存不区分 mode 的坑。
  for (int d = 0; d < 8; ++d) {
    const CellRef n = Adjacent(w, RefOf(cell), d);
    const CellCoord nc = CoordOf(w, n);
    if (!grid.InMap(nc)) continue;
    w.SetMaskCache(nc, -1);
    w.SetMaskCache(nc, ShoreMask(w, grid, theater, nc, 2));
  }
}

// ── 第 3/4 遍：直边 / 拐角（0x57ACF0，ret 0x10）────────────────────────
// which = 1 → 第 3 遍（掩码 mode 0）；which = 2 → 第 4 遍（掩码 mode 1）。
bool Pass34Shore(SmoothWorld& w, const MapGrid& grid, const TheaterData& theater,
                 Randomizer& rng, const CellCoord& cell, int which,
                 int blob_id, bool flag) {
  // 0x57ACF9：无条件先抽一个，哪怕这一格什么都不干。整阶段的随机流全靠它。
  const int r = PickRange(rng, 0, 5);

  const int mask = ShoreMask(w, grid, theater, cell, which == 2 ? 1 : 0);
  if (mask == 0) return true;                  // 0x57AD2F

  int i = 0;   // 组内下标
  if (which == 2) {
    // 四种直角拐角（0x57AD3E 起，判定顺序 西北 → 东北 → 东南 → 西南）
    if ((mask & 0xA0) == 0xA0) {
      i = ((mask & 0x11) == 0x11) ? (r & 1) + 0x17 : ((mask & 0x01) ? 0x15 : 0x1E);
    } else if ((mask & 0x82) == 0x82) {
      i = ((mask & 0x44) == 0x44) ? (r & 1) + 0x0F : ((mask & 0x04) ? 0x0E : 0x16);
    } else if ((mask & 0x0A) == 0x0A) {
      i = ((mask & 0x11) == 0x11) ? (r & 1) + 0x07 : ((mask & 0x01) ? 0x0D : 0x06);
    } else if ((mask & 0x28) == 0x28) {
      i = ((mask & 0x44) == 0x44) ? (r & 1) + 0x1F : ((mask & 0x04) ? 0x05 : 0x1D);
    } else {
      return true;                             // 0x57ADE1
    }
  } else if (mask & 0x02) {
    // 东边是水：沿岸往南数（0x57AE0F）
    int run = 1;
    CellRef a = Adjacent(w, RefOf(cell), 4);
    CellRef b = Adjacent(w, a, 2);
    while (!IsWaterRef(w, theater, a) && IsWaterRef(w, theater, b)) {
      ++run;
      a = Adjacent(w, a, 4);
      b = Adjacent(w, b, 4);
    }
    // 只有这一路把 r%3 包了一层 abs（0x57AEC1 的 cdq/xor/sub）。
    // r 落在 [0,5] 时无害，但照抄。
    i = (((run & 1) && !(mask & 0x80)) || !(mask & 0x04) || (mask & 0x18))
            ? 0x0C
            : std::abs(r % 3) + 9;
  } else if (mask & 0x20) {
    // 西边是水：同样往南数（0x57AECE）
    int run = 1;
    CellRef a = Adjacent(w, RefOf(cell), 4);
    CellRef b = Adjacent(w, a, 6);
    while (!IsWaterRef(w, theater, a) && IsWaterRef(w, theater, b)) {
      ++run;
      a = Adjacent(w, a, 4);
      b = Adjacent(w, b, 4);
    }
    i = (((run & 1) && !(mask & 0x80)) || !(mask & 0x10) || (mask & 0x0C))
            ? 0x1C
            : r % 3 + 0x19;
  } else if (mask & 0x08) {
    // 南边是水：沿岸往东数（0x57AF88）
    int run = 1;
    CellRef a = Adjacent(w, RefOf(cell), 2);
    CellRef b = Adjacent(w, a, 4);
    while (!IsWaterRef(w, theater, a) && IsWaterRef(w, theater, b)) {
      ++run;
      a = Adjacent(w, a, 2);
      b = Adjacent(w, b, 2);
    }
    // 南 / 北两路的奇偶判据里没有 !(mask & 0x80) 那一项，与东 / 西不对称。
    i = ((run & 1) || !(mask & 0x04) || (mask & 0x03)) ? 0x04 : r % 3 + 1;
  } else if (mask & 0x80) {
    // 北边是水：往东数（0x57B03B）
    int run = 1;
    CellRef a = Adjacent(w, RefOf(cell), 2);
    CellRef b = Adjacent(w, a, 0);
    while (!IsWaterRef(w, theater, a) && IsWaterRef(w, theater, b)) {
      ++run;
      a = Adjacent(w, a, 2);
      b = Adjacent(w, b, 2);
    }
    i = ((run & 1) || !(mask & 0x01) || (mask & 0x06)) ? 0x14 : r % 3 + 0x11;
  } else if (mask & 0x01) {   // 纯对角（0x57B0ED，顺序 NE → SE → SW → NW）
    i = 0x23 + (r & 1);
  } else if (mask & 0x04) {
    i = 0x21 + (r & 1);
  } else if (mask & 0x10) {
    i = 0x27 + (r & 1);
  } else if (mask & 0x40) {
    i = 0x25 + (r & 1);
  } else {
    return true;                               // 0x57B117
  }

  // 0x57B125 的 test/jle 兜底。上面每条分支都给正数，走不到。
  if (i <= 0) return true;

  // 0x57B12D：TileTypes[ShorePieces + i - 1]，那个 -4 位移让下标整体左移一格。
  const TileType* tile = w.TileByIndex(theater.shore_pieces + i - 1);
  if (tile == nullptr) return true;

  // 0x57B14C：落点 = 中心格 + 表里的 (dx, dy)，两个分量各自是 16 位加法。
  const CellCoord at{
      static_cast<std::int16_t>(cell.x + kShoreOffsetX[i]),
      static_cast<std::int16_t>(cell.y + kShoreOffsetY[i])};

  bool ok = true;                              // 0x57B192
  PlaceTileParams params;
  if (which == 1) {
    params.allow_lo = 0;                       // 0x57B1B3 / 0x57B1B5
    params.allow_hi = 0;
  } else if (which == 2) {
    params.allow_lo = theater.shore_pieces;    // 0x57B1DE / 0x57B1DD
    params.allow_hi = theater.shore_pieces + 0x29;
  } else {
    return true;                               // 0x57B1BC：既不是 1 也不是 2
  }
  params.base_level = static_cast<std::int8_t>(w.Level(cell));
  params.blob_id = blob_id;
  params.ok = &ok;
  // 第 6 个参数收的是 flag 本身。flag 为真时贴砖器里 blob_id 被清零、
  // 工作数组既不读也不写、判定恒走「同块」分支。
  params.no_blob_tracking = flag;

  // 返回值被丢弃（0x57B1E8 只看 *ok）。两者语义不同，见 place_tile.cpp。
  PlaceTile(w, grid, theater, *tile, at, params);
  return ok;
}

}  // namespace


bool ShorelineSmooth(SmoothWorld& w, const MapGrid& grid,
                     const TheaterData& theater, Randomizer& rng,
                     int blob_id, bool flag) {
  // 0x57A0C7 call 0x4A8BF0(0)：RMG 期间是空操作（Map->+0x117C == 0）。
  //
  // 0x57A0D3：工作数组为空时会临时 malloc + 逐条构造 + 收尾 free。
  // RMG 里它由阶段 1（0x578E60）建好，这条路走不到，故不建模。
  //
  // 0x57A12B：把 side² 条记录的 +0x40 全部置 -1（整个数组，不止菱形内）。
  w.ResetMaskCaches();

  // 迭代器 0x578290 的产出顺序 == MapGrid::ForEachCell。四遍各重置一次，
  // 顺序完全相同，所以取一次就够。
  std::vector<CellCoord> cells;
  cells.reserve(static_cast<std::size_t>(grid.CellCount()));
  grid.ForEachCell([&cells](const CellCoord& c) { cells.push_back(c); });

  bool ok = true;                              // 0x57A18F mov bl,1

  for (const CellCoord& c : cells) {           // 第 1 遍
    if (!ok) break;                            // 0x57A193 每格调用前先看
    ok = Pass1Erode(w, grid, theater, c, blob_id, flag);
  }
  for (const CellCoord& c : cells) {           // 第 2 遍
    if (!ok) break;                            // 0x57A1EE
    Pass2Cleanup(w, grid, theater, c, blob_id, flag);   // 返回值被彻底丢弃
  }
  for (const CellCoord& c : cells) {           // 第 3 遍：直边
    if (!ok) break;                            // 0x57A247
    ok = Pass34Shore(w, grid, theater, rng, c, 1, blob_id, flag);
  }
  for (const CellCoord& c : cells) {           // 第 4 遍：拐角
    if (!ok) break;                            // 0x57A2A4
    ok = Pass34Shore(w, grid, theater, rng, c, 2, blob_id, flag);
  }

  // 0x57A2C4：清 [0x880990]，再对 [0x88098C] 调虚表 +0x20(1) 并置空。
  // 那是「临时砖图元」的释放，不影响地图状态，不建模。
  return ok;                                   // 0x57A30B mov al, bl
}

}  // namespace rmg
}  // namespace ra2
