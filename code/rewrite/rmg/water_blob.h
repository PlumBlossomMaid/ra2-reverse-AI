// `MapType ∈ {3,4}` 支路的湖泊涂块器 `FUN_0059C920`。
//
// 它是阶段 2 主涂块器 `0x59BBC0` 的**镜像**：那个往海里涂陆地，这个往陆地里
// 涂水。同样是 Dijkstra 式最小堆扩散，但优先级里没有正态项、也没有会漂移的
// 引力中心——只有「均匀噪声 + 到起点的距离 − 已涂格数」三项，所以湖是圆盘状
// 铺开而不是枝杈状。
//
// 取证（gamemd.exe）：主体 0x59C920-0x59D436。
//   预算            0x59C92E-0x59C970
//   池/堆分配       0x59C976-0x59CA0B
//   全图清 +0x3C    0x59CA0B-0x59CA56（⚠️ **无条件**，不分 start 是否为 (0,0)）
//   禁涂带预处理    0x59CA79-0x59CADA（清 +0x44 / Erode(0,2,-2) / 置 +0x44）
//   全图放行        0x59CC81-0x59CC9C（start != (0,0) 时走这条）
//   随机起点        0x59CAFC-0x59CC74（先加计数再判合法性）
//   死出口          0x59CC6C-0x59CC7E（第二次 attempts >= 200 判定，到不了）
//   截尾正态取块大小 0x59CCA7-0x59CD7D
//   起点入堆        0x59CD81-0x59CE32
//   主循环          0x59CE5E-0x59D14B（四正邻，优先级 0x59CFC8-0x59D053）
//   收编剩余堆      0x59D14D-0x59D237（⚠️ 这里**查砖**，与主循环不一致）
//   成败判定        0x59D239-0x59D25B
//   成功收尾        0x59D26F-0x59D3B1（岸线平滑 / Dilate / 刷绿）
//   清理与回滚      0x59D29A-0x59D434
// 详见 docs/random-map/stage-water-maptype34.md §二。
#ifndef RA2_RMG_WATER_BLOB_H_
#define RA2_RMG_WATER_BLOB_H_

#include "map_grid.h"
#include "regions.h"

namespace ra2 {
namespace rmg {

// 湖泊涂块比阶段 3 的区域算子多要一样东西：岸线平滑。
class WaterBlobWorld : public RegionWorld {
 public:
  // 0x57A0C0(blob_id, flag = 0)。⚠️ flag 是 0，不是涂块那次的 1。
  virtual bool SmoothShoreline(int blob_id) = 0;
};

// MapSeedClass 上被这个函数读写的四个成员。painted_total 是**进出参**。
struct WaterBlobParams {
  int water_amount = 0;    // +0x4C  规则里的 WaterAmount
  int painted_total = 0;   // +0x304 先前几块水已经花掉的格数；成功时累加
  int blob_id = 0;         // +0x308 本块编号
  int cell_level = 4;      // +0x30C 回滚时写回 CellClass +0x11B
};

// `MapSeedClass::WaterBlob(CellStruct* start)`（0x59C920，`ret 4`，返回 char）。
//   start  两个分量都为 0 时随机取起点，并且额外做「禁涂带」预处理与成功收尾；
//          非 (0,0)（河口挖湖）时全图放行、收尾只到成败判定为止。
//
// 返回 false = 预算不够 / 200 次都没抽到合法起点 / 撞上第三方地盘 /
// 涂出来的格数不达标 / 岸线铺不出来——后几种都会整块回滚。
//
// ⚠️ 必须在 FpuChopScope 作用域内调用。
bool WaterBlob(RegionContext& ctx, WaterBlobWorld& world, WaterBlobParams& p,
               const CellCoord& start);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_WATER_BLOB_H_
