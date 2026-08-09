// 阶段 9「Creating hills」的第 ② 步：连续高度场 0x5A2F50。
//
// 整个造山阶段**只有这里取随机数**（RNG 总账里的 0x598003，即 NormalDeviate
// 内部那次 UniformUnit）。它是一个顺序推进的分形高度场：按线性下标
// Y*side + X 从小到大走，每格拿左邻与上邻的平均值当基线，再抽两次截断正态
// ——一次更新 slope（下一次抽样的 sigma），一次更新 height。
//
// ⚠️ Ruggedness < 10 时 `baseSlope < 0.025`，函数在 0x5A2FA8 直接返回，
// **一个随机数都不抽**，整张图一座山也没有。
//
// 取证：docs/random-map/stage-hills.md §二。
#ifndef RA2_RMG_HEIGHT_FIELD_H_
#define RA2_RMG_HEIGHT_FIELD_H_

#include <vector>

#include "blob_paint.h"
#include "fast_math.h"
#include "map_grid.h"
#include "place_tile.h"   // TileWorld / IsShoreTile / IsUndecidedTile / 地形保护
#include "theater.h"

namespace ra2 {
namespace rmg {

// 0x5A3215 / 0x5A32ED 两处共用的骨架。
//
//   if (mu - sigma > hi || mu + sigma < lo) { sigma = (hi - lo) * 0.5; mu = sigma + lo; }
//   do { v = NormalDeviate() * sigma + mu; } while (v < lo || v > hi);
//
// 两端都是**闭区间**接受（`v < lo` 与 `v > hi` 才重抽）。
// NormalDeviate 一次 Box-Muller 出两个值并缓存第二个，所以一次调用消耗的
// Random() 个数不是定值——缓存状态必须由调用方跨阶段延续。
double TruncNormal(NormalDeviate& nd, double mu, double sigma, double lo,
                   double hi);

// 0x5A33A4 起那三条分支：四舍五入**远离零**，而且 0.0 原样不动。
// 不是 ties-to-even，也不是 std::round（那个对 -0.0 的处理不同）。
double RoundHalfAwayFromZero(double h);

// 0x5A33F0 MarkHeightAnchors()。造山的第 ① 步，**一个随机数都不取**。
//
// 按原版顺序扫全图，对每个格子分两类：
//   · 岸砖（0x4865B0）        -> 找第一个「还没定砖」的邻格，给它 height = 0.5
//                               并打锚点标记 flag45
//   · 受保护地形（0x4863D0）  -> 同样找第一个未定砖邻格，但**只打标记**，
//                               不给初始高度
// 两支都是全 8 向、命中第一个就 `break`；邻格必须在菱形内（四条不等式与
// MapGrid::InMap 逐条相同）。锚点在 0x5A2F50 里会被钳到 height >= 0，
// 且 slope 强制回 0.0025 —— 于是岸边与悬崖/桥附近永远是平地。
//
// ⚠️ 谓词读的是**当前格**的砖号 / 子格号，写的是**邻格**的工作记录；
// 而这一遍从不改砖号，所以结果与扫描顺序无关（多个格子写同一个邻格时
// 写的值也相同）。顺序仍然照抄，别改。
//
// 取证：docs/random-map/stage-hills.md §一。
void MarkHeightAnchors(TileWorld& world, const MapGrid& grid,
                       const TheaterData& theater,
                       std::vector<PaintWorkCell>& work);

// 0x5A2F50 GenerateHeightField(Ruggedness)。
// `work` 是 side² 条工作记录，`grid` 用整图矩形 (W+4, H+12) 构造
// ——菱形判定读的是 [0xABED04] / [0xABED08]，正是这一对。
// 进来之前 0x5A33F0 应已把锚点（`flag45` 与 `height = 0.5`）打好。
void GenerateHeightField(const MapGrid& grid, std::vector<PaintWorkCell>& work,
                         int ruggedness, NormalDeviate& nd);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_HEIGHT_FIELD_H_
