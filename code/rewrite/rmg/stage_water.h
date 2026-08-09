// 阶段 2「Seeding water」的调度器 `FUN_0059A6C0`——把前面那些零件串成一条线。
//
// 它自己不长陆地，只负责编排：先把整张图铺成水，按 MapType 分派投放策略，
// 补掉四面环陆的孤水格，再对**整张图**做一次岸线平滑，最后沿着岸砖往外刷一圈绿地。
//
// 取证（gamemd.exe）：主体 0x59A6C0-0x59A8E0。
//   (a) 0x59A6C7-0x59A6F4  全图 +0x38 = [0xAA0738]
//   (b) 0x59A6F6-0x59A71D  switch(+0x3C) → 0x59AD10 / 0x59AFA0 / 0x59B200
//   (c) 0x59A71D-0x59A77D  孤水格回填（四正邻全「未定砖」→ +0x38 = 0）
//   (d) 0x59A77F-0x59A788  Shoreline(0, 0)  ← flag 是 0，不是涂块那次的 1
//   (e) 0x59A78D-0x59A7C0  工作数组 side² 条的 +0x38/+0x3C 全写 -1
//   (f) 0x59A7C2-0x59A86F  RegionClass 数组拆除（不碰地形）
//   (g) 0x59A87A          [0xABED14] = 0（区域编号计数器归零）
//   (h) 0x59A884-0x59A8D9  岸砖格的四正邻若「未定砖」→ +0x38 = [0xAA0E18]
// 详见 docs/random-map/stage-seeding-water.md §一 / §九。
#ifndef RA2_RMG_STAGE_WATER_H_
#define RA2_RMG_STAGE_WATER_H_

#include "blob_paint.h"
#include "place_blobs.h"
#include "shore_smooth.h"

namespace ra2 {
namespace rmg {

// 阶段 2 要同时扮演涂块的世界（PaintWorld）与平滑的世界（SmoothWorld）。
//
// ⚠️ `IsoTile` 在 PaintWorld / ShoreWorld / TileWorld 三个基类里各声明一次，
// `SetIsoTile` 在 PaintWorld / TileWorld 里各一次。实现方**各写一份**即可同时
// 覆盖，但调用点的非限定名查找会二义，所以统一走下面两个非虚壳子。
class WaterStageWorld : public PaintWorld, public SmoothWorld {
 public:
  // 读走 SmoothWorld::Tile()（它内部已经限定到 ShoreWorld 子对象）。
  void SetTile(const CellCoord& c, int v) {
    static_cast<TileWorld*>(this)->SetIsoTile(c, v);
  }

  // 0x59A7A5-0x59A7C0：把 side² 条工作记录的 +0x38（块归属）与 +0x3C（入堆标记）
  // 全部写 **-1**（不是 0），整个方阵，不止菱形内。
  // ⚠️ 原版里这两个字段与 PaintWorkCell.blob_id / .queued、以及
  // TileWorld::WorkBlob 读写的是**同一片内存**；实现方必须一次全清干净。
  virtual void ResetWorkOwners() = 0;
};

// `MapSeedClass::SeedWater()`（0x59A6C0，`ret 0`，this = MapSeedClass）。
// ctx.world 会被就地改写成 &world。
//
// ⚠️ 必须在 FpuChopScope 作用域内调用（投放策略与平滑都走 x87）。
void SeedWater(PaintContext& ctx, WaterStageWorld& world, const PlaceParams& p,
               int map_type);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_STAGE_WATER_H_
