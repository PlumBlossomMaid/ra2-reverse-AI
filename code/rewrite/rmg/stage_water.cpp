#include "stage_water.h"

#include <cstddef>
#include <vector>

#include "cell_ref.h"

namespace ra2 {
namespace rmg {

void SeedWater(PaintContext& ctx, WaterStageWorld& world, const PlaceParams& p,
               int map_type) {
  ctx.world = &world;
  const MapGrid& grid = ctx.grid;
  const TheaterData& theater = *ctx.theater;

  // 迭代器（0x578350 Reset / 0x578290 Next）在这个函数里被重置三次，
  // 每次产出顺序都一样，取一份复用。
  std::vector<CellCoord> cells;
  cells.reserve(static_cast<std::size_t>(grid.CellCount()));
  grid.ForEachCell([&cells](const CellCoord& c) { cells.push_back(c); });

  // ── (a) 0x59A6DF：全图铺水。只写 +0x38，subtile / level 一概不动 ──
  for (const CellCoord& c : cells) world.SetTile(c, theater.water_set);

  // ── (b) 0x59A6F6：按 MapType 分派投放策略 ──
  // 三个分支各自把 [0x308] 置 1、清 +0x3C，然后反复调涂块核心。
  // MapType 不在 {0,1,2} 里时原版**什么都不做**（0x59A702 的 jne 直接落到收尾）。
  PlaceBlobs(ctx, p, map_type);

  // ── (c) 0x59A737：孤水格回填 ──
  // 判据是「砖号**恰好等于** WaterSet 起始砖」（0x59A73F 的 cmp 是等值比较，
  // 不是公共的 IsWater 区间判定）——刚铺完水还没被岸线改过的那种格子。
  // 四个正邻若全是「未定砖」（0x486380：0 或 0xFFFF，也就是陆地），就把自己也变陆。
  //
  // ⚠️ 就地扫描：本轮改成 0 的格子会立刻影响后面格子的判定，顺序不能动。
  // ⚠️ 四个方向**全部求值**，没有短路（原版用 bl 标志攒结果）；
  //    出图的邻格会退化成共享哑元，读到的是哑元当前的 +0x38。
  for (const CellCoord& c : cells) {
    if (world.Tile(c) != theater.water_set) continue;
    bool all_undecided = true;                  // 0x59A743 mov bl,1
    // 0x59A747 `for (esi = 0; esi < 8; esi += 2)`：步长 2，只看八邻域表
    // 0x89F688 的偶数项——四个**正**方向 N / E / S / W，对角一概不看
    for (int dir = 0; dir < 8; dir += 2) {
      const CellRef n = Adjacent(world, RefOf(c), dir);
      if (!IsUndecided(TileOf(world, n))) all_undecided = false;
    }
    if (all_undecided) world.SetTile(c, 0);     // 0x59A768
  }

  // ── (d) 0x59A77F：对整张图做一次岸线平滑 ──
  // blob_id = 0、flag = **0**（涂块收尾那次传的是 1，两者语义不同）。
  // 返回值被丢弃（0x59A78D 紧接着就去读 [0xABED10]）。
  ShorelineSmooth(world, grid, theater, *ctx.rng, 0, false);

  // ── (e) 0x59A78D：工作数组的块归属与入堆标记整片作废 ──
  world.ResetWorkOwners();

  // ── (f) 0x59A7C2-0x59A86F：RegionClass 数组（[0xABDF94] / [0xABDFA0]）
  //        逐个析构 + 从 vector 里摘除 + free。全程不碰任何格子，不建模。
  // ── (g) 0x59A87A：[0xABED14] = 0，区域编号计数器归零。
  //        那是阶段 3「Init regions」的输入，与地形无关，不建模。

  // ── (h) 0x59A899：沿岸刷绿 ──
  // 中心格必须是岸砖（0x4865B0：tile ∈ [ShorePieces, +0x2A)），
  // 四个正邻里凡是「未定砖」的，就写成 GreenTile。
  //
  // ⚠️ 写的是**邻格**，而且出图的邻格是共享哑元——哑元一旦被写成 GreenTile
  // 就不再「未定」，后面所有出图邻格都会被跳过。这个副作用是可观测的。
  for (const CellCoord& c : cells) {
    if (!IsShoreTile(world.Tile(c), theater.shore_pieces)) continue;
    // 0x59A8A6 `for (esi = 0; esi < 8; esi += 2)`：步长 2，只看八邻域表
    // 0x89F688 的偶数项——四个**正**方向 N / E / S / W，对角一概不看
    for (int dir = 0; dir < 8; dir += 2) {
      const CellRef n = Adjacent(world, RefOf(c), dir);
      if (!IsUndecided(TileOf(world, n))) continue;
      if (n.dummy) {
        world.SetDummyIsoTile(theater.green_tile);
      } else {
        world.SetTile(n.coord, theater.green_tile);
      }
    }
  }
}

}  // namespace rmg
}  // namespace ra2
