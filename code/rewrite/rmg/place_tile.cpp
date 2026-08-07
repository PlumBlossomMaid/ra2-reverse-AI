#include "place_tile.h"

#include <cstdint>
#include <cstdlib>

namespace ra2 {
namespace rmg {

// 0x82A7F4：岸砖"族"。索引 = 砖号 - ShorePieces ∈ [0, 0x29]。
const int kShoreFamily[42] = {
  0, 0, 0, 1, 2, 3, 4, 4, 5, 5, 5, 6, 7, 8, 9, 9, 10, 10, 10, 11, 12,
  13, 14, 14, 15, 15, 15, 16, 17, 18, 19, 19, 20, 20, 21, 21, 22, 22,
  23, 23, 24, 25,
};

// 0x82A89C：岸砖"朝向"，取值 0..7。
const int kShoreFacing[42] = {
  4, 4, 4, 4, 4, 4, 3, 3, 2, 2, 2, 2, 2, 2, 1, 1, 0, 0, 0, 0, 0, 0,
  7, 7, 6, 6, 6, 6, 6, 6, 5, 5, 3, 3, 1, 1, 7, 7, 5, 5, 4, 4,
};

namespace {

// 四组瀑布共用的形状：组宽 4，只有组内第 0 与第 3 块要看子格号，
// 第 1/2 块无条件保护。两个 a/b 是"这个子格号反而放行"的两个例外。
struct FallGroup { int base; int a; int b; };

// 返回 -1 = 这组没命中，继续往下查；0/1 = 命中并给出结论。
int MatchFall(const FallGroup& f, int tile, int subtile) {
  if (tile < f.base || tile >= f.base + 4) return -1;
  if (tile != f.base && tile != f.base + 3) return 1;
  return (subtile != f.a && subtile != f.b) ? 1 : 0;
}

}  // namespace

// 0x4863D0，ecx = cell（砖号取 +0x38、子格号取 +0x11A）。
// 十组，顺序与原版逐条一致；每组都先与 -1 比一次——该 theater 没定义
// 这一组时整段跳过，不会因为"-1 恰好落在区间里"而误伤。
bool IsProtectedTerrainCell(const TheaterData& t, int tile, int subtile) {
  if (t.cliff_set != -1 && tile >= t.cliff_set &&
      tile < t.cliff_set + 0x28) {                        // 0x4863D0
    return true;
  }
  const FallGroup falls[4] = {
    {t.waterfall_east,  0, 4},   // 0x4863EE
    {t.waterfall_west,  1, 3},   // 0x486431
    {t.waterfall_south, 0, 1},   // 0x486475
    {t.waterfall_north, 2, 3},   // 0x4864B8
  };
  for (const FallGroup& f : falls) {
    if (f.base == -1) continue;
    const int r = MatchFall(f, tile, subtile);
    if (r >= 0) return r != 0;
  }
  if (t.cliff_ramps != -1 && tile >= t.cliff_ramps &&
      tile < t.cliff_ramps + 0x14) {                      // 0x4864FC
    return true;
  }
  if (t.water_caves != -1 && tile >= t.water_caves &&
      tile < t.water_caves + 4) {                         // 0x486519
    return true;
  }
  if (t.bridge_set != -1 && tile >= t.bridge_set &&
      tile < t.bridge_set + 0x10) {                       // 0x486536
    return true;
  }
  if (t.wood_bridge_set != -1 && tile >= t.wood_bridge_set &&
      tile < t.wood_bridge_set + 0x10) {                  // 0x486553
    return true;
  }
  if (t.destroyable_cliffs != -1 && tile >= t.destroyable_cliffs &&
      tile < t.destroyable_cliffs + 2) {                  // 0x486570
    return true;
  }
  if (t.water_cliffs != -1 && tile >= t.water_cliffs &&
      tile < t.water_cliffs + 0x1C) {                     // 0x48658D
    return true;
  }
  return false;
}

// 0x578D80，ecx = 砖号、edx = 子格号，由调用方直接给出。
// 只有六组（比上面那份少四组：WaterCaves / BridgeSet / WoodBridgeSet /
// DestroyableCliffs / WaterCliffs 都不在，反而多了 CliffRamps 兜底），
// 而且**一处 -1 前置判断都没有**。两份判据不一致是原版就有的，照抄不合并。
bool IsProtectedTerrainTile(const TheaterData& t, int tile, int subtile) {
  if (tile >= t.cliff_set && tile < t.cliff_set + 0x28) return true;  // 0x578D80
  const FallGroup falls[4] = {
    {t.waterfall_east,  0, 4},   // 0x578D95
    {t.waterfall_west,  1, 3},   // 0x578DC9
    {t.waterfall_south, 0, 1},   // 0x578DF2
    {t.waterfall_north, 2, 3},   // 0x578E1A
  };
  for (const FallGroup& f : falls) {
    const int r = MatchFall(f, tile, subtile);
    if (r >= 0) return r != 0;
  }
  return tile >= t.cliff_ramps && tile < t.cliff_ramps + 0x14;        // 0x578E43
}

namespace {

// 0x57B51C-0x57B535：格子指针表 [0x87F924] 用 `Y*512 + X` 寻址。索引为负、
// >= 0x40000、或表里那一格是 NULL 时，退到一个**静态哑元格子** 0xABDC50，
// 并把坐标写进它的 +0x24（0xABDC74）。哑元是全局共享的一份，上一次贴砖
// 留在它 +0x38 上的砖号会被下一次读到——原版就是这样，照样保留。
int CellPointerIndex(const CellCoord& c) { return (c.y << 9) + c.x; }

}  // namespace

bool PlaceTile(TileWorld& world, const MapGrid& grid, const TheaterData& theater,
               const TileType& tile, const CellCoord& at,
               const PlaceTileParams& params) {
  // 0x57B440-0x57B486：RTTI 必须是 0x12，图元指针必须非空。
  // 这两条不满足都是 `return false` 且**不碰 *ok**（跳 0x57B784）。
  if (tile.cells.empty()) return false;

  // 0x57B45B：no_blob_tracking 非零时把归属块编号强行清零。
  const int blob_id = params.no_blob_tracking ? 0 : params.blob_id;

  const int shore = theater.shore_pieces;
  const int new_tile = tile.tile_number;

  // 外层 ty 走 +0x2E8（高），内层 tx 走 +0x2E4（宽）；<= 0 则一格都不贴，
  // 直接落到 0x57B6AD 返回成功。
  for (int ty = 0; ty < tile.height; ++ty) {          // 0x57B4A8
    for (int tx = 0; tx < tile.width; ++tx) {         // 0x57B4BC
      // 落点 + 子格偏移。两个分量各自先截成 int16 再符号扩展
      // （0x57B4C4-0x57B4E1 全程在 cx/ax 上算），所以会绕回。
      const CellCoord wc{
          static_cast<std::int16_t>(at.x + tx),
          static_cast<std::int16_t>(at.y + ty)};

      // 菱形边界，四条比较与 MapGrid::InMap 逐条对应（0x57B4E8-0x57B516）
      if (!grid.InMap(wc)) continue;

      // 格子指针；拿不到就用共享哑元
      const int pi = CellPointerIndex(wc);
      const bool real = pi >= 0 && pi < 0x40000 && world.CellExists(wc);
      if (!real) world.SetDummyCoord(wc);

      // 子格在 .tmp 偏移表里的序号；表项为 0 = 空洞，整格跳过
      // （0x57B542-0x57B561）。注意 k 用的是**砖的宽**，不是地图宽。
      const int k = tile.width * ty + tx;
      if (!tile.At(tx, ty).present) continue;

      const int cur_tile = real ? world.IsoTile(wc) : world.DummyIsoTile();
      const int ni = new_tile - shore;

      bool claim = false;   // 这一格要不要改写工作数组的归属

      if (params.no_blob_tracking) {
        // 0x57B576 走 0x57B57E：blob_id 已被清零，`0 == blob_id` 必然成立，
        // 于是恒定走"同块"的朝向差检查，工作数组从头到尾不读也不写。
        const int oi = cur_tile - shore;
        if (oi >= 0 && ni >= 0 && oi <= 0x29 && ni <= 0x29) {
          const int d = std::abs(kShoreFacing[oi] - kShoreFacing[ni]);
          if (d >= 3 && d <= 5) {                      // 0x57B5CE-0x57B5D6
            if (params.ok) *params.ok = false;         // 0x57B724
            return false;
          }
        }
      } else {
        const int work = world.WorkBlob(wc);           // 0x57B571
        if (work == blob_id) {
          // ── 同一块：同上的朝向差检查（0x57B58C）
          const int oi = cur_tile - shore;
          if (oi >= 0 && ni >= 0 && oi <= 0x29 && ni <= 0x29) {
            const int d = std::abs(kShoreFacing[oi] - kShoreFacing[ni]);
            if (d >= 3 && d <= 5) {
              if (params.ok) *params.ok = false;
              return false;
            }
          }
        } else if (work > 0 && blob_id != -1) {
          // ── 属于**别的**块（0x57B5EF）
          if (IsUndecidedTile(cur_tile)) {
            claim = true;                              // 抢过来
          } else {
            const int oi = cur_tile - shore;
            if (oi < 0 || ni < 0 || oi > 0x29 || ni > 0x29 ||
                kShoreFamily[oi] != kShoreFamily[ni]) {
              if (params.ok) *params.ok = false;        // 0x57B711
              return false;
            }
            // ⚠️ 同族就**直接判定整块成功返回**，后面的子格一格都不贴。
            // 看着像漏了个 continue，但 0x57B703 -> 0x57B705 确实是
            // `mov al,1; ret`。照原样保留。
            return true;
          }
        } else {
          // ── 其余（work <= 0 且不等于 blob_id，或 blob_id == -1）：0x57B6B6
          if (IsUndecidedTile(cur_tile)) {
            claim = true;
          } else if (blob_id != -1) {
            if (params.ok) *params.ok = false;          // 0x57B6D9 -> 0x57B77D
            return false;
          } else {
            continue;                                   // 0x57B6DF：跳过这一格
          }
        }
      }

      if (claim) world.SetWorkBlob(wc, blob_id);        // 0x5A0090

      // ── 写入前的最后一道闸（0x57B62C 起）。已定砖且不在"允许覆盖"区间里
      // 才会被拦；三条出口全是 return false，区别只在动不动 *ok。
      if (!IsUndecidedTile(cur_tile) &&
          (cur_tile < params.allow_lo || cur_tile > params.allow_hi)) {
        if (IsShoreTile(cur_tile, shore) &&
            IsProtectedTerrainTile(theater, new_tile, k)) {   // 0x57B739/0x57B74E
          if (params.ok) *params.ok = false;                  // 0x57B77D
          return false;
        }
        const int sub = real ? world.SubTile(wc) : world.DummySubTile();
        if (new_tile >= shore && new_tile < shore + 0x2A &&
            IsProtectedTerrainCell(theater, cur_tile, sub)) { // 0x57B772
          if (params.ok) *params.ok = false;
          return false;
        }
        // ⚠️ 走到这里 return false 却**不动 *ok**（0x57B784）。而调用方
        // 0x57B1E8 只看 *ok、不看返回值——所以这条路对调用方等于"没失败"。
        return false;
      }

      // ── 写入（0x57B650-0x57B672）。注意 no_blob_tracking 也照写不误。
      const int lvl = tile.At(tx, ty).height + params.base_level;
      if (real) {
        world.SetIsoTile(wc, new_tile);
        world.SetSubTile(wc, static_cast<std::int8_t>(k));
        world.SetLevel(wc, static_cast<std::int8_t>(lvl));
      } else {
        world.SetDummyTile(new_tile, static_cast<std::int8_t>(k),
                           static_cast<std::int8_t>(lvl));
      }
    }
  }
  return true;                                          // 0x57B6AD
}

}  // namespace rmg
}  // namespace ra2
