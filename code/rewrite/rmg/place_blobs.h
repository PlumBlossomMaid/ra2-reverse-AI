// 三种 MapType 的投放策略——决定"在哪里、投多少、投几次"涂块。
//
// 涂块核心（blob_paint.h）只管"从一个种子长出一坨陆地"，这三个函数才是
// 大陆分布的决策者：MapType 0 网格分区各投一坨，MapType 1 从地图正中贪心
// 长到目标覆盖率，MapType 2 把可用区一刀切两半各自长。
//
// 取证（gamemd.exe）：0x59AD10 / 0x59AFA0 / 0x59B200，
// 分派点在 0x59C580 附近。详见 docs/random-map/stage-seeding-water.md §二。
#ifndef RA2_RMG_PLACE_BLOBS_H_
#define RA2_RMG_PLACE_BLOBS_H_

#include "blob_paint.h"
#include "blob_region.h"

namespace ra2 {
namespace rmg {

// 三者共用的入口参数，全部来自 MapSeedClass 与地图全局。
struct PlaceParams {
  int num_players = 2;    // MapSeedClass +0x50
  int water_amount = 0;   // MapSeedClass +0x4C
  Rect usable;            // [0x87F8E4..F0] 可用区域（真实坐标）
};

// 0x59AD10。把可用区切成 NumPlayers + PickRange(1, max(2, NumPlayers/2)) 个矩形，
// 每个矩形投一坨，最多重试 10 次。
void PlaceBlobsGrid(PaintContext& ctx, const PlaceParams& p);

// 0x59AFA0。起点在地图正中，每轮补齐"还差多少覆盖率"，
// 下一轮起点 = 全图中尚未入过堆、且离**地图正中**曼哈顿距离最近的那格。
void PlaceBlobsCentre(PaintContext& ctx, const PlaceParams& p);

// 0x59B200。抛硬币决定横切还是竖切，两半各自贪心生长；
// 下一轮起点按"椭圆内 + 各向异性加权距离最近"挑。
void PlaceBlobsHalves(PaintContext& ctx, const PlaceParams& p);

// map_type 0/1/2 的分派；其余取值原版不走这条路，这里直接不做事。
// ⚠️ 必须在 FpuChopScope 作用域内调用。
void PlaceBlobs(PaintContext& ctx, const PlaceParams& p, int map_type);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_PLACE_BLOBS_H_
