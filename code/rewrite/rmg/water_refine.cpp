#include "water_refine.h"

#include <vector>

#include "cell_ref.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {

namespace {

// 0x59C6A4 等六处：邻格的判据是「砖号**等于** WaterSet」且「子格号 == 0」。
// 注意不是判水区间 [WaterSet, +0x0E)——阶段 2 的孤水回填也用的等值比较。
bool IsPlainWater(const SmoothWorld& w, const CellRef& r, int water) {
  const int tile = r.dummy ? w.DummyIsoTile() : w.Tile(r.coord);
  if (tile != water) return false;
  const int sub = r.dummy ? w.DummySubTile() : w.SubTile(r.coord);
  return static_cast<signed char>(sub) == 0;   // 原版是 `cmp byte ptr [..], bl`
}

}  // namespace

void RefineWaterTiles(SmoothWorld& w, const MapGrid& grid,
                      const TheaterData& theater, Randomizer& rng,
                      bool has_work) {
  const int water = theater.water_set;

  std::vector<CellCoord> cells;
  cells.reserve(static_cast<std::size_t>(grid.CellCount()));
  grid.ForEachCell([&cells](const CellCoord& c) { cells.push_back(c); });

  for (const CellCoord& c : cells) {
    // 0x59C657/0x59C668：中心格自己也要是「纯水 + 第 0 子格」
    if (!IsPlainWater(w, RefOf(c), water)) continue;

    // 0x59C674-0x59C68E：三次 AdjacentCell，顺序 **2(E) → 4(S) → 3(SE)**，
    // 而且是**先全取完再判**。取的过程会改写共享哑元的坐标，顺序不能动。
    const CellRef e = Adjacent(w, RefOf(c), 2);
    const CellRef s = Adjacent(w, RefOf(c), 4);
    const CellRef se = Adjacent(w, RefOf(c), 3);

    bool big = IsPlainWater(w, e, water) && IsPlainWater(w, s, water) &&
               IsPlainWater(w, se, water);

    if (big) {
      // 0x59C6DA：PickFrom1(10)，抽到 1（10%）就退回小水砖那一支
      if (PickFrom1(rng, 10) == 1) {
        big = false;
      } else {
        // 0x59C70F：PickBelow(242)。0..239 走 /40 → 形状 0..5；
        // 240 → 7、241 → 6（0x59C742 的 `247 - n`）。八块大水砖里
        // 6 号与 7 号各只有 1/242 的机会，这条怪癖照抄。
        const int n = PickBelow(rng, 242);
        const int shape = (n >= 240) ? (247 - n) : (n / 40);

        // 0x59C75E：工作数组在就取这一格的归属，不在就传 -1
        const int blob = has_work ? w.WorkBlob(c) : -1;

        const TileType* t = w.TileByIndex(water + shape);
        // 原版没有空指针保护（0x5A6C1E 直接 `mov ebp,[eax+ecx*4]` 再解虚表），
        // 砖表里缺这块砖就是崩。这里跳过，随机数已经照样吃掉了。
        if (t != nullptr) StampTile(w, grid, *t, c, blob, -1);
        continue;
      }
    }

    // 0x59C79C：PickBelow(201) → WaterSet + 8 + n / 40 ∈ [+8, +13]。
    // WaterSet 一共 0x0E 块，前 8 块是 2×2 大水、后 6 块正好是这里的小水砖。
    const int n = PickBelow(rng, 201);
    w.SetIsoTile(c, water + 8 + n / 40);
  }
}

}  // namespace rmg
}  // namespace ra2
