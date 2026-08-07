// 水面细化 `FUN_0059C630`——把整片单调的水面换成"大块水"与"小浪花"两种花砖。
//
// 它排在阶段 2 之后、阶段 3 之前，由 `0x598B14` **无条件**调用
// （不像旁边的 `0x59C580` 只在 `MapType ∈ {3,4}` 且 `WaterAmount != 0` 时跑）。
// 逻辑很简单：扫全图，凡是「自己 + 东 + 南 + 东南」四格都是 WaterSet 的
// 第 0 子格，就有 90% 的机会压一块 2×2 的大水砖；否则给自己换一块小水砖。
//
// 它是阶段 2 之后**第一个还在消耗随机数**的地方，取数次数错一次，
// 后面所有阶段的随机流全错位。
//
// 取证（gamemd.exe）：主体 0x59C630-0x59C7F8（`ret 0`，this = MapClass 迭代器）。
//   0x59C63A/0x59C644  迭代器 Reset 0x578350 / Next 0x578290（顺序 == ForEachCell）
//   0x59C657-0x59C6D4  中心格与东/南/东南三邻的 `+0x38 == [0xAA0738] && +0x11A == 0`
//   0x59C6DA           PickFrom1(10)；== 1 就落到小水砖那一支
//   0x59C70F           PickBelow(242) → 形状号
//   0x59C795           StampTile(WaterSet + 形状, &cell->+0x24, blob, **-1**)
//   0x59C79C           PickBelow(201) → +0x38 = WaterSet + 8 + n / 40
// 详见 docs/random-map/stage-seeding-water.md §六。
#ifndef RA2_RMG_WATER_REFINE_H_
#define RA2_RMG_WATER_REFINE_H_

#include "map_grid.h"
#include "randomizer.h"
#include "shore_smooth.h"
#include "theater.h"

namespace ra2 {
namespace rmg {

// `MapSeedClass::RefineWaterTiles()`（0x59C630）。
//
// has_work 对应 `[0xABED10] != 0`（工作数组在不在）。为假时压砖传的归属是
// **-1** 而不是格子的归属；不过 StampTile 那边的写入也同样被这面旗挡掉，
// 所以两处必须给同一个值。RMG 主路径上它恒为真。
//
// ⚠️ 必须在 FpuChopScope 作用域内调用（两处取数都走 x87）。
void RefineWaterTiles(SmoothWorld& world, const MapGrid& grid,
                      const TheaterData& theater, Randomizer& rng,
                      bool has_work = true);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_WATER_REFINE_H_
