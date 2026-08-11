// 坡道与架桥——阶段 4「Making regions」把区域连起来的两种方式。
//
// 取证（gamemd.exe）：
//   0x590970 BuildRamp（8 个取数点：四对端点抖动）+ 0x590FD0 邻域探测
//   四个直坡 0x5910F0 南 / 0x591740 东 / 0x591D80 北 / 0x592440 西（各 1 取数）
//   三个转角 0x593AF0 N+E / 0x593550 E+S / 0x593030 S+W（0 取数）
//   架桥 0x58F2C0（5 取数点）
// 详见 docs/random-map/stage-making-regions.md §五之二 ~ §五之四。
#include "ramps.h"

#include <algorithm>
#include <cstdint>

#include "scaled_random.h"
#include "start_points.h"   // AreaIsPassable（0x5A7250）
#include "zones.h"

namespace ra2 {
namespace rmg {
namespace {

// 0x89F688：0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW。
const int kDeltaX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int kDeltaY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

inline CellCoord Step(const CellCoord& c, int dir) {
  return CellCoord{static_cast<std::int16_t>(c.x + kDeltaX[dir]),
                   static_cast<std::int16_t>(c.y + kDeltaY[dir])};
}

inline std::size_t WorkIdx(const RegionContext& ctx, const CellCoord& c) {
  const long long i =
      static_cast<long long>(c.y) * ctx.grid.side() + c.x;
  if (i < 0) return 0;
  const long long n = static_cast<long long>(ctx.work->size());
  return static_cast<std::size_t>(i >= n ? n - 1 : i);
}

inline PaintWorkCell& Rec(RegionContext& ctx, const CellCoord& c) {
  return (*ctx.work)[WorkIdx(ctx, c)];
}

// 0x5A00C0 GetWorkOwner：work 为空返回 -1。
inline int Owner(const RegionContext& ctx, const CellCoord& c) {
  if (ctx.work == nullptr) return -1;
  return (*ctx.work)[WorkIdx(ctx, c)].blob_id;
}

// 0x5A0090 SetWorkOwner：work 为空静默无操作。
inline void SetOwner(RegionContext& ctx, const CellCoord& c, int v) {
  if (ctx.work == nullptr) return;
  (*ctx.work)[WorkIdx(ctx, c)].blob_id = v;
}

// 0x5A6C10 压砖器在肩膀砖/桥头/变体重掷里的统一入口。
inline void StampByIndex(MakingWorld& world, const MapGrid& grid, int index,
                         const CellCoord& at, int blob_id, int level) {
  const TileType* t = world.TileByIndex(index);
  if (t == nullptr) return;
  StampTile(world, grid, *t, at, blob_id, level);
}

// 取数点 0x5914A4 系：PickBelow(2)（`do { coin = _ftol(Random()×2/(2³²−1)); }
// while (coin > 1);`）。
inline int Coin(Randomizer& rng) {
  int v;
  do {
    v = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
             2.0 * kInvUintMax);
  } while (v > 1);
  return v;
}

// 刷带：x ∈ [x0, x1)、y ∈ [y0, y1)，每格过菱形才写 owner 与层高。
inline void PaintBand(RegionContext& ctx, MakingWorld& w, int owner_id,
                      int level, int x0, int x1, int y0, int y1) {
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const CellCoord c{x, y};
      if (!ctx.grid.InMap(c)) continue;
      SetOwner(ctx, c, owner_id);
      w.SetLevel(c, static_cast<std::int8_t>(level));
    }
  }
}

// 台阶段/直坡段的落格：**直接写 CellClass 字段**，不经贴砖器
// （反编译 0x591xxx 里逐条 `+0x11B/+0x11C/+0x11A/+0x38` 写）。0x5A6C10 只用于
// 肩膀砖那一处。`write_owner` 只在直坡段为 true（台阶段不写 owner）。
inline void WriteRampCell(RegionContext& ctx, MakingWorld& w,
                          const CellCoord& c, int tile_idx, int slope_idx,
                          int level, bool write_owner, int owner_id) {
  if (!ctx.grid.InMap(c)) return;
  w.SetIsoTile(c, tile_idx);
  w.SetSubTile(c, 0);
  w.SetLevel(c, static_cast<std::int8_t>(level));
  w.SetRamp(c, slope_idx);
  if (write_owner) SetOwner(ctx, c, owner_id);
}

// ── 四个直坡共用的表 ──
// 层高表 {3,2,1,0,0} 四份内容相同（0x82AF3C/78/B4/F0）。
const int kLevelTable[5] = {3, 2, 1, 0, 0};
// 台阶表两行（d < 0 / d > 0），各方向不同，按方向查。
const int kSlopeSouth[2][5] = {{11, 15, 15, 15, 7}, {12, 16, 16, 16, 8}};
const int kSlopeEast[2][5]  = {{10, 14, 14, 14, 6}, {11, 15, 15, 15, 7}};
const int kSlopeNorth[2][5] = {{10, 14, 14, 14, 6}, {9, 13, 13, 13, 5}};
const int kSlopeWest[2][5]  = {{9, 13, 13, 13, 5}, {12, 16, 16, 16, 8}};

// ⚠️ 本文件当前只有南向（0x5910F0）的刷带/台阶段坐标逐条核过反编译
// （rmg_ramp_stage4j.txt）；东/北/西三向按"同一份代码转 90°"的对称推导，
// 具体坐标待对拍校正（见 §五之二 未确认 2）。

// ── 南向直坡 0x5910F0（坡道沿 X 方向，a.X → b.X）──
bool BuildStraightSouth(RegionContext& ctx, MakingWorld& world,
                        RegionClass* hi, const CellCoord& a,
                        const CellCoord& b, int low_id) {
  const int dx = b.x - a.x;
  const int dy = b.y - a.y;
  const int min_y = std::min(a.y, b.y);
  const int max_y = std::max(a.y, b.y);
  const int hi_level = hi->level;

  const Rect r{a.x - 5, min_y - 3, dx + 9, (max_y - min_y) + 11};
  if (!Clearance(ctx, world, hi->id, r, low_id)) return false;
  if (dx - 5 < std::abs(dy)) return false;                  // 斜度限制
  const int half = (dx + 1) / 2 + 3;

  // 四条刷带（半开区间，反编译钉死）
  PaintBand(ctx, world, low_id, ctx.regions->ById(low_id)->level,
            a.x - 5, a.x + half - 3, a.y + 1, max_y + 10);          // 带1 low
  PaintBand(ctx, world, low_id, ctx.regions->ById(low_id)->level,
            b.x - half + 3, b.x + 5, b.y + 1, max_y + 10);          // 带2 low
  PaintBand(ctx, world, hi->id, hi_level,
            a.x - 1, a.x + half - 3, min_y - 1, a.y + 1);           // 带3 high
  PaintBand(ctx, world, hi->id, hi_level,
            b.x - half + 2, b.x + 1, min_y - 1, b.y + 1);           // 带4 high

  // 两块肩膀砖（StampTile，level = 高侧层高）
  const int ssp = ctx.theater->slope_set_pieces;
  StampByIndex(world, ctx.grid, ssp + 0, a, low_id, hi_level);
  StampByIndex(world, ctx.grid, ssp + 1, CellCoord{b.x - 2, b.y},
               low_id, hi_level);

  // 硬币：斜台阶段贴跑道哪一头（dy == 0 时不取）
  int jog = 0, flat_x = 0, flat_y = 0, slope_row = 0;
  if (dy != 0) {
    const int coin = Coin(*ctx.rng);
    jog = (coin == 1) ? 0 : dx - std::abs(dy) - 5;
    if (coin == 1) {
      flat_x = std::abs(dy);
      flat_y = dy;
    }
    slope_row = (dy > 0) ? 1 : 0;
  }

  // 台阶段：i ∈ [0, |dy|)、j ∈ [0, 5)
  const int ramp_base = ctx.theater->ramp_base;
  for (int i = 0; i < std::abs(dy); ++i) {
    for (int j = 0; j < 5; ++j) {
      const int yy = a.y + j + (dy < 0 ? -1 : 0) + (dy > 0 ? i : -i);
      const int s = kSlopeSouth[slope_row][j];
      WriteRampCell(ctx, world, CellCoord{a.x + 3 + i + jog, yy},
                    ramp_base + s - 1, s, hi_level + kLevelTable[j] - 4,
                    false, low_id);
    }
  }

  // 直坡段：i ∈ [0, len)、j ∈ [0, 4)，len = dx - |dy| - 5
  const int len = dx - std::abs(dy) - 5;
  for (int i = 0; i < len; ++i) {
    for (int j = 0; j < 4; ++j) {
      WriteRampCell(ctx, world, CellCoord{a.x + 3 + i + flat_x,
                                          a.y + j + flat_y},
                    ramp_base + 3, 4, hi_level - j - 1, true, low_id);
    }
  }
  return true;
}

// ── 东/北/西向直坡：坐标公式待子代理从反编译提取后补全（见本文件头部注释）──
bool BuildStraightEast(RegionContext&, MakingWorld&, RegionClass*, const CellCoord&,
                       const CellCoord&, int) {
  return false;   // TODO(subagent)：0x591740 坐标
}
bool BuildStraightNorth(RegionContext&, MakingWorld&, RegionClass*, const CellCoord&,
                        const CellCoord&, int) {
  return false;   // TODO(subagent)：0x591D80 坐标
}
bool BuildStraightWest(RegionContext&, MakingWorld&, RegionClass*, const CellCoord&,
                       const CellCoord&, int) {
  return false;   // TODO(subagent)：0x592440 坐标
}

// ── 三个转角：坐标公式待子代理提取后补全 ──
bool BuildCornerNE(RegionContext&, MakingWorld&, RegionClass*, const CellCoord&,
                   const CellCoord&, int) {
  return false;   // TODO(subagent)：0x593AF0
}
bool BuildCornerES(RegionContext&, MakingWorld&, RegionClass*, const CellCoord&,
                   const CellCoord&, int) {
  return false;   // TODO(subagent)：0x593550
}
bool BuildCornerSW(RegionContext&, MakingWorld&, RegionClass*, const CellCoord&,
                   const CellCoord&, int) {
  return false;   // TODO(subagent)：0x593030
}

}  // namespace

// ───────────────────────── 0x590970 BuildRamp ─────────────────────────

bool BuildRamp(RegionContext& ctx, MakingWorld& world, RegionClass* hi,
               const CellCoord& cell, int low_region_id, float tol) {
  // 0x590FD0 邻域探测（内联）：中心 5×5 盒子必须够 need 格。
  // `need = _ftol(15.0 − 5.0) × (1.0f − tol) + 5.0`——照抄浮点顺序
  // （先 (15−5)=10.0，再 (1.0f−tol)，再乘，再 +5.0）。
  const double need_f =
      (15.0 - 5.0) * (1.0 - static_cast<double>(tol)) + 5.0;
  const int need = Ftol(need_f);
  const Rect center{cell.x - 2, cell.y - 2, 5, 5};
  if (!CountAtLeast(ctx, center, low_region_id, need)) return false;

  // 0x82AF18 方向位表：j+3i 的下标，i 走 Y、j 走 X。
  static const int kDirBit[9] = {0x40, 0x80, 0x01, 0x20, 0, 0x02,
                                 0x10, 0x08, 0x04};
  unsigned mask = 0;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      if (i == 1 && j == 1) continue;
      const Rect r{cell.x - 7 + 5 * j, cell.y - 7 + 5 * i, 5, 5};
      if (CountAtLeast(ctx, r, low_region_id, need)) {
        mask |= static_cast<unsigned>(kDirBit[j + 3 * i]);
      }
    }
  }
  if (mask == 0xFF) return false;                       // 0x590970 的 0xff 短路

  bool made = false;
  // 分支1：N+E 有、S/SW/W 无 → 0x593AF0（0 取数）
  if ((mask & 0x82) == 0x82 && (mask & 0x38) == 0) {
    CellCoord a{cell.x - 1, cell.y - 5};
    CellCoord b{cell.x + 5, cell.y + 1};
    if (mask & 0x40) b.y = cell.y - 4;                 // NW 高
    if (mask & 0x04) b.x = cell.x + 4;                 // SE 高
    made = BuildCornerNE(ctx, world, hi, a, b, low_region_id);
    if (made) return true;
  }
  // 分支2：E+S 有、NE/SW 无 → 0x593550
  if ((mask & 0x0A) == 0x0A && (mask & 0x11) == 0) {
    CellCoord a{cell.x + 5, cell.y - 1};
    CellCoord b{cell.x - 1, cell.y + 5};
    if (mask & 0x10) b.y = cell.y + 4;                 // SW 高
    if (mask & 0x01) a.x = cell.x + 4;                 // NE 高
    made = BuildCornerES(ctx, world, hi, a, b, low_region_id);
    if (made) return true;
  }
  // 分支3：S+W 有、N/NE/E 无 → 0x593030
  if ((mask & 0x28) == 0x28 && (mask & 0x83) == 0) {
    CellCoord a{cell.x + 1, cell.y + 5};
    CellCoord b{cell.x - 5, cell.y - 1};
    if (mask & 0x40) b.x = cell.x - 6;                 // NW 高
    if (mask & 0x04) a.y = cell.y + 4;                 // SE 高
    made = BuildCornerSW(ctx, world, hi, a, b, low_region_id);
    if (made) return true;
  }
  // 分支4：W+N 有、E/SE/S 无 → 复用 0x593030（**永远失败**的死分支）
  if ((mask & 0xA0) == 0xA0 && (mask & 0x0E) == 0) {
    CellCoord a{cell.x - 5, cell.y + 1};
    CellCoord b{cell.x + 1, cell.y - 5};
    if (mask & 0x01) b.y = cell.y - 4;                 // NE 高
    if (mask & 0x10) a.x = cell.x - 4;                 // SW 高
    made = BuildCornerSW(ctx, world, hi, a, b, low_region_id);
    if (made) return true;                             // 实际永远到不了
  }
  // 分支5：西侧三格全无、N 或 S 有 → 0x592440 西直坡（2 取数）
  if ((mask & 0x70) == 0 && (mask & 0x88) != 0) {
    CellCoord a{cell.x - 1, cell.y - 4};
    CellCoord b{cell.x - 1, cell.y + 4};
    if (mask & 0x80) { a.y = cell.y - 3; b.y = cell.y + 5; }   // N 高
    if (mask & 0x08) { a.y -= 1; b.y -= 1; }                   // S 高
    a.x += Coin(*ctx.rng);                              // 抖动 1（0x590BAE）
    b.x += Coin(*ctx.rng);                              // 抖动 2（0x590BDD）
    made = BuildStraightWest(ctx, world, hi, a, b, low_region_id);
    if (made) return true;
  }
  // 分支6：东侧三格全无、N 或 S 有 → 0x591740 东直坡
  if ((mask & 0x07) == 0 && (mask & 0x88) != 0) {
    CellCoord a{cell.x + 1, cell.y + 4};
    CellCoord b{cell.x + 1, cell.y - 4};
    if (mask & 0x80) { b.y = cell.y - 3; a.y = cell.y + 5; }   // N 高
    if (mask & 0x08) { a.y -= 1; b.y -= 1; }                   // S 高
    a.x += Coin(*ctx.rng) - 1;                          // 抖动 1（0x590C7B）
    b.x += Coin(*ctx.rng) - 1;                          // 抖动 2（0x590CAB）
    made = BuildStraightEast(ctx, world, hi, a, b, low_region_id);
    if (made) return true;
  }
  // 分支7：北侧三格全无、E 或 W 有 → 0x591D80 北直坡
  if ((mask & 0xC1) == 0 && (mask & 0x22) != 0) {
    CellCoord a{cell.x + 4, cell.y - 1};
    CellCoord b{cell.x - 4, cell.y - 1};
    if (mask & 0x20) { b.x = cell.x - 3; a.x = cell.x + 5; }   // W 高
    if (mask & 0x02) { a.x -= 1; b.x -= 1; }                   // E 高
    a.y += Coin(*ctx.rng);                              // 抖动 1（0x590D4A）
    b.y += Coin(*ctx.rng);                              // 抖动 2（0x590D79）
    made = BuildStraightNorth(ctx, world, hi, a, b, low_region_id);
    if (made) return true;
  }
  // 分支8：南侧三格全无、E 或 W 有 → 0x5910F0 南直坡
  if ((mask & 0x1C) == 0 && (mask & 0x22) != 0) {
    CellCoord a{cell.x - 4, cell.y + 1};
    CellCoord b{cell.x + 4, cell.y + 1};
    if (mask & 0x20) { a.x = cell.x - 3; b.x = cell.x + 5; }   // W 高
    if (mask & 0x02) { b.x -= 1; a.x -= 1; }                   // E 高
    a.y += Coin(*ctx.rng) - 1;                          // 抖动 1（0x590E17）
    b.y += Coin(*ctx.rng) - 1;                          // 抖动 2（0x590E47）
    made = BuildStraightSouth(ctx, world, hi, a, b, low_region_id);
    if (made) return true;
  }

  // 后路：tol > 0.5f（tries ≥ 51，RC=chop 下 50×0.01f 截成 0.49999997f）且
  // 前面八条全败，才追加四次跨度固定 ±4、两端不抖动的直坡尝试。
  if (tol > 0.5f && !made) {
    if ((mask & 0x08) == 0) {                           // S 无
      if (BuildStraightSouth(ctx, world, hi, CellCoord{cell.x - 4, cell.y + 1},
                             CellCoord{cell.x + 4, cell.y + 1},
                             low_region_id)) {
        return true;
      }
    }
    if ((mask & 0x02) == 0) {                           // E 无
      if (BuildStraightEast(ctx, world, hi, CellCoord{cell.x + 1, cell.y + 4},
                            CellCoord{cell.x + 1, cell.y - 4},
                            low_region_id)) {
        return true;
      }
    }
    if ((mask & 0x08) == 0) {                           // ⚠️ 复制粘贴门：又是 S 无
      if (BuildStraightNorth(ctx, world, hi, CellCoord{cell.x + 4, cell.y - 1},
                             CellCoord{cell.x - 4, cell.y - 1},
                             low_region_id)) {
        return true;
      }
    }
    if ((mask & 0x20) == 0) {                           // W 无
      if (BuildStraightWest(ctx, world, hi, CellCoord{cell.x - 1, cell.y - 4},
                            CellCoord{cell.x - 1, cell.y + 4},
                            low_region_id)) {
        return true;
      }
    }
  }
  return made;
}

// ───────────────────────── 0x5A7250 / 0x5A7440 ─────────────────────────

// 0x5A7250 的阶段 4 适配版（AreaIsPassable）：四角过菱形 + 逐格五条砖谓词。
bool AreaPassable(const RegionContext& ctx, MakingWorld& world, const Rect& r,
                  bool allow_road, bool allow_road_end) {
  const int x0 = static_cast<std::int16_t>(r.x);
  const int y0 = static_cast<std::int16_t>(r.y);
  const int x1 = static_cast<std::int16_t>(r.x + r.w - 1);
  const int y1 = static_cast<std::int16_t>(r.y + r.h - 1);
  if (!ctx.grid.InMap(x0, y0) || !ctx.grid.InMap(x1, y0) ||
      !ctx.grid.InMap(x0, y1) || !ctx.grid.InMap(x1, y1)) {
    return false;
  }
  const TheaterData& t = *ctx.theater;
  for (int y = r.y; y < r.y + r.h; ++y) {
    for (int x = r.x; x < r.x + r.w; ++x) {
      const CellCoord c{static_cast<std::int16_t>(x),
                        static_cast<std::int16_t>(y)};
      const int tile = world.IsoTile(c);
      if (IsPavedRoadTile(tile, t.paved_roads)) {
        if (!allow_road) return false;
        continue;
      }
      if (IsPavedRoadEndTile(tile, t.paved_road_ends)) {
        if (!allow_road_end) return false;
        continue;
      }
      if (IsUndecidedTile(tile)) continue;
      if (IsMiscPaveTile(tile, t.misc_pave_tile)) continue;
      if (IsPaveTile(tile, t.pave_tile)) continue;
      return false;
    }
  }
  return true;
}

// 0x5A7440：AreaIsPassable **再加**「每格 +0x11B 都等于左上角那格」。
bool AreaSameLevel(const RegionContext& ctx, MakingWorld& world, const Rect& r,
                   bool allow_paved_road) {
  if (!AreaPassable(ctx, world, r, allow_paved_road, false)) return false;
  const int base = world.Level(CellCoord{r.x, r.y});
  for (int y = r.y; y < r.y + r.h; ++y) {
    for (int x = r.x; x < r.x + r.w; ++x) {
      if (world.Level(CellCoord{x, y}) != base) return false;
    }
  }
  return true;
}

// ───────────────────────── 0x58F2C0 BuildBridge ─────────────────────────

// 0x5902C0：桥的落脚区检查。以 rect 为左上角的 (W+1)×(H+1) 闭区间。
// 锚格 = rect 左上角那格（0x5902EB 取 +0x11B 当基准，不是调用方传入）。
bool BridgeFootprintClear(const RegionContext& ctx, MakingWorld& world,
                          const Rect& r) {
  const int anchor_level = world.Level(CellCoord{r.x, r.y});   // 0x5902EB
  for (int y = r.y; y <= r.y + r.h; ++y) {
    for (int x = r.x; x <= r.x + r.w; ++x) {
      const CellCoord c{x, y};
      if (world.Overlay(c) != -1) return false;         // +0x44 == -1
      if (world.Level(c) != anchor_level) return false;
      const int tile = world.IsoTile(c);
      if (!IsUndecidedTile(tile) &&
          !IsWaterOrShoreTile(tile, *ctx.theater)) {
        return false;
      }
    }
  }
  return true;
}

// 0x5904B0：在 (W+1)×(H+1) 闭区间里找第一格「没有 overlay、是平地、+0xE4 == 0」。
bool PlaceRepairHut(const RegionContext& ctx, MakingWorld& world,
                    const Rect& r) {
  for (int y = r.y; y <= r.y + r.h; ++y) {
    for (int x = r.x; x <= r.x + r.w; ++x) {
      const CellCoord c{x, y};
      if (world.Overlay(c) != -1) continue;             // +0x44
      if (!IsUndecidedTile(world.IsoTile(c))) continue; // 0x486380
      // +0xE4 在 CLI 世界里恒为 0（无对象），照原版语义视为通过。
      world.PlaceRepairHut(c);
      return true;
    }
  }
  return false;
}

namespace {

inline bool IsOrigin(const CellCoord& c) { return c.x == 0 && c.y == 0; }

// 探条侧翼检查：`IsWithinUsableArea(cell, 1)` + `IsCliffOrBridgeTile`（0x4863D0）。
inline bool ProbeSideOK(const RegionContext& ctx, MakingWorld& world,
                        const CellCoord& c) {
  const MapRect usable{ctx.usable.x, ctx.usable.y, ctx.usable.w,
                       ctx.usable.h};
  if (!IsWithinUsableArea(ctx.map_rect_w, usable, c.x, c.y,
                          world.Level(c), world.Ramp(c), true)) {
    return false;
  }
  // 0x4863D0：读格子的砖号与子格号（cell 版本，每个全局先查 -1）。
  return !IsProtectedTerrainCell(*ctx.theater, world.IsoTile(c),
                                 world.SubTile(c));
}

// 桥头路砖的 StampTile 入口：`StampTile(tileIdx, &coord, -1, -1)`。
// 0x5A6D3A **无条件写** work.+0x38 = -1（文档 §五之二"别动 owner"是错的，
// 以汇编为准），level == -1 跳过层高写入。
inline void StampRoadEnd(MakingWorld& world, const MapGrid& grid, int index,
                         const CellCoord& at) {
  const TileType* t = world.TileByIndex(index);
  if (t == nullptr) return;
  StampTile(world, grid, *t, at, -1, -1);
}

}  // namespace

bool BuildBridge(RegionContext& ctx, MakingWorld& world, RegionClass* water,
                 RegionClass* a, RegionClass* b) {
  const int id_a = a->id, id_b = b->id;
  const int side = ctx.grid.side();
  const int n_cells = side * side;

  int tries = 0;
  char done = 0;
  for (;;) {
    if (done) return done != 0;                        // 成功后从循环头出去

    // 第一步：抽一格水面当锚点——三重拒绝采样（取数点 0x58F342）。
    int idx;
    do {
      do {
        do {
          idx = Ftol(static_cast<double>(static_cast<std::uint32_t>(
                         ctx.rng->Random())) *
                     kInvUintMax * static_cast<double>(n_cells));
        } while (idx > n_cells - 1);                   // PickBelow(side²)
      } while ((*ctx.work)[static_cast<std::size_t>(idx)].blob_id !=
               water->id);                             // ① 必须是本水域的格
    } while (IsOrigin((*ctx.work)[static_cast<std::size_t>(idx)].coord));
    const CellCoord anchor = (*ctx.work)[static_cast<std::size_t>(idx)].coord;
    const int x = anchor.x, y = anchor.y;

    // 第二步：四条探条（成对短路：北失败不试南、西失败不试东）。
    // 循环判据在走之前：锚点那条 3×1/1×3 一开始就可跨时循环体一次不执行。
    bool ok_v = false, ok_h = false;
    int y_n = 0, y_s = 0, x_w = 0, x_e = 0;

    {  // 竖桥：北 + 南
      Rect strip{x - 1, y, 3, 1};
      while (!AreaPassable(ctx, world, strip, false, false)) {
        strip.y -= 1;
        const CellCoord l{strip.x, strip.y};
        const CellCoord r{strip.x + 2, strip.y};
        if (!ProbeSideOK(ctx, world, l) || !ProbeSideOK(ctx, world, r)) {
          goto fail_v;
        }
      }
      if (!AreaPassable(ctx, world, Rect{strip.x, strip.y - 3, 3, 3}, false,
                        false)) {
        goto fail_v;
      }
      y_n = strip.y;
      // 南（北失败时整段跳过——共用 fail_v 标签）
      strip = Rect{x - 1, y, 3, 1};
      while (!AreaPassable(ctx, world, strip, false, false)) {
        strip.y += 1;
        const CellCoord l{strip.x, strip.y};
        const CellCoord r{strip.x + 2, strip.y};
        if (!ProbeSideOK(ctx, world, l) || !ProbeSideOK(ctx, world, r)) {
          goto fail_v;
        }
      }
      if (!AreaPassable(ctx, world, Rect{strip.x, strip.y + 1, 3, 3}, false,
                        false)) {
        goto fail_v;
      }
      y_s = strip.y;
      ok_v = true;
    }
  fail_v:
    ;  // 北失败跳到这——y_s 未赋值、ok_v 保持 false，南段没走

    {  // 横桥：西 + 东
      Rect strip{x, y - 1, 1, 3};
      while (!AreaPassable(ctx, world, strip, false, false)) {
        strip.x -= 1;
        const CellCoord l{strip.x, strip.y};
        const CellCoord r{strip.x, strip.y + 2};
        if (!ProbeSideOK(ctx, world, l) || !ProbeSideOK(ctx, world, r)) {
          goto fail_h;
        }
      }
      if (!AreaPassable(ctx, world, Rect{strip.x - 3, strip.y, 3, 3}, false,
                        false)) {
        goto fail_h;
      }
      x_w = strip.x;
      // 东（西失败时整段跳过）
      strip = Rect{x, y - 1, 1, 3};
      while (!AreaPassable(ctx, world, strip, false, false)) {
        strip.x += 1;
        const CellCoord l{strip.x, strip.y};
        const CellCoord r{strip.x, strip.y + 2};
        if (!ProbeSideOK(ctx, world, l) || !ProbeSideOK(ctx, world, r)) {
          goto fail_h;
        }
      }
      if (!AreaPassable(ctx, world, Rect{strip.x + 1, strip.y, 3, 3}, false,
                        false)) {
        goto fail_h;
      }
      x_e = strip.x;
      ok_h = true;
    }
  fail_h:
    ;  // 西失败跳到这——x_e 未赋值、ok_h 保持 false，东段没走

    // 第三步：配对判定——两端必须正好是 A 和 B（顺序任意）。
    const int span_v = ok_v ? std::abs(y_s - y_n) : 999;
    const int span_h = ok_h ? std::abs(x_e - x_w) : 999;
    // 落桥点区域号：竖桥读探条自己的列 x-1，横桥读探条自己的行 y-1
    //（探条矩形**左上角**，不是锚点本身）。
    const auto owner_at = [&](int xx, int yy) {
      return (*ctx.work)[static_cast<std::size_t>(yy * side + xx)].blob_id;
    };
    if (ok_v) {
      const int n_id = owner_at(x - 1, y_n);
      const int s_id = owner_at(x - 1, y_s);
      if (!((n_id == id_a && s_id == id_b) ||
            (n_id == id_b && s_id == id_a))) {
        ok_v = false;
      }
    }
    if (ok_h) {
      const int w_id = owner_at(x_w, y - 1);
      const int e_id = owner_at(x_e, y - 1);
      if ((e_id == id_a && w_id == id_b) ||
          (e_id == id_b && w_id == id_a)) {
        if (ok_v) {
          // 两个方向都可用时选短的；spanV == spanH 走 else 砍竖桥——平手偏横。
          if (span_v < span_h) {
            ok_h = false;
          } else {
            ok_v = false;
          }
        }
      } else {
        ok_h = false;
      }
    }

    // 第四步：跨距上限随重试次数放宽，先横后竖。
    const int max_span = tries / 25 + 8;
    Rect rect{0, 0, 0, 0};                             // 每轮开头清 0
    if (ok_h && span_h < max_span) {
      rect = Rect{x_w, y - 1, span_h + 1, 3};
    } else if (ok_v && span_v < max_span) {
      rect = Rect{x - 1, y_n, 3, span_v + 1};
    }
    if (rect.w > 0 && rect.h > 0) {
      if (!BridgeFootprintClear(ctx, world, rect)) goto next_try;

      // 第五步：铺桥面（不取随机数）。
      if (rect.h == 3) {                               // 横桥
        for (int yy = rect.y; yy < rect.y + rect.h; ++yy) {
          for (int xx = rect.x; xx < rect.x + rect.w; ++xx) {
            const CellCoord c{xx, yy};
            if (xx == rect.x) {
              world.SetOverlay(c, 0x5E);               // LOBRDG21 西端
            } else if (xx == rect.x + rect.w - 1) {
              world.SetOverlay(c, 0x5C);               // LOBRDG19 东端
            } else {
              world.SetOverlay(c, 0x4A + xx % 4);      // LOBRDG01..04，有符号取余
            }
            world.SetOverlayData(c, static_cast<std::int8_t>(yy - rect.y));
          }
        }
      } else {                                         // 竖桥
        for (int yy = rect.y; yy < rect.y + rect.h; ++yy) {
          for (int xx = rect.x; xx < rect.x + rect.w; ++xx) {
            const CellCoord c{xx, yy};
            if (yy == rect.y) {
              world.SetOverlay(c, 0x60);               // LOBRDG23 北端
            } else if (yy == rect.y + rect.h - 1) {
              world.SetOverlay(c, 0x62);               // LOBRDG25 南端
            } else {
              world.SetOverlay(c, 0x53 + yy % 4);      // LOBRDG10..13
            }
            world.SetOverlayData(c, static_cast<std::int8_t>(xx - rect.x));
          }
        }
      }

      // 第六步：桥头路砖——4 个 PickBelow(2)，横桥只走东/西，竖桥只走北/南。
      // 每个端：试探矩形同高（0x5A7440，allowRoad = 0）才抛硬币；
      // 不平照样贴 PavedRoadEnds（不抽随机数）。
      const int roads = ctx.theater->paved_roads;
      const int road_ends = ctx.theater->paved_road_ends;
      if (rect.h == 3) {                               // 横桥：东 + 西
        if (AreaSameLevel(ctx, world, Rect{rect.x + rect.w, rect.y - 2, 6, 6},
                          false)) {                    // 0x58FBDD
          const int coin = Coin(*ctx.rng);
          StampRoadEnd(world, ctx.grid,
                       coin == 1 ? roads + 10 : road_ends + 0,
                       CellCoord{rect.x + rect.w, rect.y});
        } else {
          StampRoadEnd(world, ctx.grid, road_ends + 0,
                       CellCoord{rect.x + rect.w, rect.y});
        }
        if (AreaSameLevel(ctx, world, Rect{rect.x - 6, rect.y - 2, 6, 6},
                          false)) {                    // 0x58FCC6
          const int coin = Coin(*ctx.rng);
          StampRoadEnd(world, ctx.grid,
                       coin == 1 ? roads + 9 : road_ends + 2,
                       CellCoord{rect.x - 4, rect.y});
        } else {
          StampRoadEnd(world, ctx.grid, road_ends + 2,
                       CellCoord{rect.x - 1, rect.y});
        }
      } else {                                         // 竖桥：北 + 南
        if (AreaSameLevel(ctx, world, Rect{rect.x - 2, rect.y - 6, 7, 6},
                          false)) {                    // 0x59000E
          const int coin = Coin(*ctx.rng);
          StampRoadEnd(world, ctx.grid,
                       coin == 1 ? roads + 13 : road_ends + 1,
                       CellCoord{rect.x, rect.y - 4});
        } else {
          StampRoadEnd(world, ctx.grid, road_ends + 1,
                       CellCoord{rect.x, rect.y - 1});
        }
        if (AreaSameLevel(ctx, world,
                          Rect{rect.x - 2, rect.y + rect.h, 7, 6},
                          false)) {                    // 0x5900F7
          const int coin = Coin(*ctx.rng);
          StampRoadEnd(world, ctx.grid,
                       coin == 1 ? roads + 12 : road_ends + 3,
                       CellCoord{rect.x, rect.y + rect.h});
        } else {
          StampRoadEnd(world, ctx.grid, road_ends + 3,
                       CellCoord{rect.x, rect.y + rect.h});
        }
      }

      // 第七步：桥梁维修小屋——两端各试两个矩形（小的不行就试大的）。
      if (rect.h == 3) {                               // 横桥
        PlaceRepairHut(ctx, world, Rect{rect.x, rect.y - 1, 2, 5});
        PlaceRepairHut(ctx, world, Rect{rect.x - 1, rect.y - 2, 3, 7});
        PlaceRepairHut(ctx, world,
                       Rect{rect.x + rect.w - 2, rect.y - 1, 2, 5});
        PlaceRepairHut(ctx, world,
                       Rect{rect.x + rect.w - 2, rect.y - 2, 3, 7});
      } else {                                         // 竖桥
        PlaceRepairHut(ctx, world, Rect{rect.x - 1, rect.y, 5, 2});
        PlaceRepairHut(ctx, world, Rect{rect.x - 2, rect.y - 1, 7, 3});
        PlaceRepairHut(ctx, world,
                       Rect{rect.x - 1, rect.y + rect.h - 2, 5, 2});
        PlaceRepairHut(ctx, world,
                       Rect{rect.x - 2, rect.y + rect.h - 2, 7, 3});
      }
      done = 1;
    }

  next_try:
    if (199 < ++tries) return done != 0;               // 判据在循环尾
  }
}

}  // namespace rmg
}  // namespace ra2

