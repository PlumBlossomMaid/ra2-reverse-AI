// MapType 3/4 支路：河流生成器 `FUN_0059D510` 与调度器 `FUN_0059C580`
//
// 取证（gamemd.exe）：
//   调度     0x59C580-0x59C91E（blobId 自增、最多重试 10 次的河与湖）
//   河流     0x59D510-0x59E73C（起点/角度/宽度抽样、逐步推进、瀑布、支流、收尾）
// 详见 docs/random-map/stage-water-maptype34.md §一、§四。
#ifndef RA2_RMG_RIVERS_H_
#define RA2_RMG_RIVERS_H_

#include "regions.h"
#include "water_blob.h"   // WaterBlobWorld / WaterBlobParams
#include "waterfall.h"    // WaterfallWorld / WaterfallParams

namespace ra2 {
namespace rmg {

// 河流生成器所需的世界接口——叠了三层：区域算子、湖泊涂块、瀑布。
// 实现方只写一份即可（三者同名成员实现一致）。
class RiverWorld : public WaterBlobWorld, public WaterfallWorld {
 public:
  // 两个基类各声明了这几个，非限定名查找会二义，壳子消歧义。
  // 通过 WaterBlobWorld 路径消歧义（两条 RegionWorld 继承路径中取一条）
  int Tile(const CellCoord& c) const {
    return static_cast<const WaterBlobWorld*>(this)->IsoTile(c);
  }
  void SetTile(const CellCoord& c, int v) {
    static_cast<WaterBlobWorld*>(this)->SetIsoTile(c, v);
  }
  // SmoothShoreline: 唯一实现由具体子类提供，满足两个基类的纯虚。
};

// MapSeedClass 上被这两个函数读写的成员。
struct RiverParams {
  int map_type = 0;       // +0x3C  仅用于调度判断
  int water_amount = 0;   // +0x4C  决定预算与最大河宽
  int painted_total = 0;  // +0x304 先前已花掉的格数；成功时累加（被湖参数复用）
  int blob_id = 0;        // +0x308 块编号；河成功后 +1、湖成功后再 +1
  int cell_level = 4;     // +0x30C 回滚时写回 CellClass +0x11B
  bool allow_waterfall = false;  // +0x310 25% 抛硬币决定这张图有没有瀑布
};

// `MapSeedClass::River(start, angle, isBranch)`（0x59D510，`ret 0x10`）。
//   start    两个分量都为 0 时自行抽起点，否则用传入坐标（支流走这条）
//   angle    初始角度（弧度）；start == (0,0) 时自行抽
//   is_branch  1 = 这是支流，不再生支流也不会下切成峡谷
//
// 返回 false = 起点出界 / 涂格数不足 / 撞第三方地盘 / 岸线铺不出 …
// ⚠️ 必须在 FpuChopScope 作用域内调用。
bool River(RegionContext& ctx, RiverWorld& world, RiverParams& p,
           const CellCoord& start, double angle, bool is_branch);

// `MapSeedClass::AddRiversAndLakes()`（0x59C580，`ret 0`）。
// 调度：如果 MapType∈{3,4} 且 WaterAmount > 20，先最多重试 10 次生河；
// 无论如何都最多重试 10 次生湖。两者成功各让 blobId +1。
void AddRiversAndLakes(RegionContext& ctx, RiverWorld& world, RiverParams& p);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_RIVERS_H_
