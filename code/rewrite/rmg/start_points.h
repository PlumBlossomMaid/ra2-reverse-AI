// 阶段 6「Creating starting points」——把出生点摆到地图上。
//
// 调度 0x594B50 PlaceStartPoints，全阶段的取数点只有三处：
//   0x5948CD  PickBelow(3)              每个区域一次，无条件抽
//   0x594935  PickBelow(region.cells)   候选采样，每次循环一次
//   0x594EA1  UniformUnit() < 0.5       只在 MapType == 0 且区域空手时
// 最远点采样 ChooseWaypoints（0x594F40）一个随机数都不取。
//
// 六步（见 docs/random-map/stage-starting-points.md §二）：
//   0) 重建寻路分区（0x56C510），算出 want = zoneType[zoneSel[MapType]][largest]，
//      再把上一轮的区域全拆掉（0x58CE90）
//   1) minCells = (int)max(H * W * 0.03, 400.0)
//   2) 扫全图，对每个还没归属、格子 +0x4C == 0、移动区类型 == want 的格子
//      起一次 GrowRegion（0x594420）；跑完把格数不足 minCells 的区域倒序销毁
//   3) 按 (500000 - cellCount) * regionCount + regionId 升序排（qsort 0x7C8B48）
//   4) 按面积比例把 NumPlayers 个名额分给各区域（累计取整，最后一个吃余数）
//   5) 逐区域 PickPointsInRegion(base)（0x594870）
//   6) MapType == 0 时给空手的区域各抛一次硬币补建筑（0x595400）
//
// ⚠️ 外层重试循环（0x598E30）只会跑一遍：验收函数 0x5A1FB0 所有出口都 return 1。
#ifndef RA2_RMG_START_POINTS_H_
#define RA2_RMG_START_POINTS_H_

#include <vector>

#include "blob_paint.h"   // PaintWorkCell：+0x38 owner / +0x3C queued
#include "map_grid.h"
#include "randomizer.h"
#include "regions.h"
#include "theater.h"
#include "zones.h"

namespace ra2 {
namespace rmg {

// 栈上的 5 项常量表（0x594B60 起把 {5,5,5,0} 与一个 0 依次摆进栈帧），
// 下标是 MapType。取 5 = 「两栖以外的地面单位」那一行，取 0 = 步兵那一行。
extern const int kStartZoneSel[5];

// 0x89F688，运行期由 0x49F2F0 填。等距坐标下的八邻域。
extern const int kAdjacentCellDX[8];
extern const int kAdjacentCellDY[8];

// 阶段 6 要问外界的几件事。做成虚接口是因为它们各自还挂着别的子系统
// （砖表、坡道、寻路分区），单测里给合成实现即可。
class StartPointWorld {
 public:
  virtual ~StartPointWorld() {}

  // CellClass +0x4C：这一格所属的寻路区编号（0x56C510 写）。阶段 6 要求候选格
  // 的这个字段是 **0**——注意 0x56C510 的洪泛只涂得到「不是出图」的格子，
  // 所以 +0x4C == 0 实际筛掉的是所有被涂过的格子。照抄谓词，不要改写成 != 0。
  virtual int CellZoneId(const CellCoord& c) const = 0;

  // MapClass::GetMovementZoneType(&coord, mz, 0)（0x56D230）
  virtual int MovementZoneType(const CellCoord& c, int movement_zone) const = 0;

  // CellClass +0x38 砖号。0x5A7250 的五条谓词全靠它。
  virtual int IsoTile(const CellCoord& c) const = 0;
  // CellClass +0x11B 层高（**有符号字节**，0x57868C 的 movsx）与 +0x11C 坡道号。
  // 0x578640 只在这两样上做文章。
  virtual int Level(const CellCoord& c) const = 0;
  virtual int Ramp(const CellCoord& c) const = 0;

  // 0x5657A0 拿不到真实格子时把请求坐标写进共享哑元 [0xABDC74]；
  // GrowRegion 是**先取格再判界**，所以这个副作用必须能复现。
  // 返回值是「哑元回读到的坐标」——真实格子返回 c 自己。
  virtual CellCoord ReadBackCoord(const CellCoord& c) = 0;

  // 0x594870 尾巴：写 ScenarioClass 的第 index 号 waypoint，并给该格
  // CellClass +0x140 |= 4
  virtual void SetWaypoint(int index, const CellCoord& c) = 0;

  // 0x5950C2：把 ScenarioClass +0x11C0..+0x11DC 这 8 个 dword 全写成 [0xABE300]。
  // ⚠️ 这一段在**每个区域**的 PickPointsInRegion 开头都跑一次，而随后写入
  // 又总是从下标 0 开始（不是从 base 开始），所以这张表最后只留得下**最后一个
  // 区域**的出生点。看着像 bug，但 waypoint 表那份是对的，照抄即可。
  virtual void ResetStartLocationTable() = 0;
  virtual void SetStartLocation(int slot, const CellCoord& c) = 0;

  // 0x595400 PlaceRandomBuilding。返回值无人读，这里也不读。
  virtual void PlaceRandomBuilding(RegionClass* r) = 0;
};

struct StartPointContext {
  MapGrid grid;
  int side = 0;                                 // [0x89C2DC] = W + H + 1
  std::vector<PaintWorkCell>* work = nullptr;   // [0xABED10]，side² 条
  RegionSet* regions = nullptr;
  Randomizer* rng = nullptr;
  StartPointWorld* world = nullptr;

  int num_players = 2;      // [0xABE028] = MapSeedClass +0x50
  int map_type = 0;         // [0xABE014] = MapSeedClass +0x3C
  int waypoint_scale = 0;   // [0xABE030] = MapSeedClass +0x58（来源未确认）
  int map_w = 0;            // [0xABE158] = MapSeedClass +0x180
  int map_h = 0;            // [0xABE15C] = MapSeedClass +0x184
  // 0x578640 读的是 **MapClass +0xF4**（[0x87F8DC]），与上面那个 MapSeedClass
  // 的 W 是两个来源；实测同值，但字段分开留着，别混用。
  int map_rect_w = 0;

  // 0x5A7250 的四个「铺装」谓词要的砖号基址
  const TheaterData* theater = nullptr;

  // 第 0 步的产物，由 ComputeStartZoneSelection() 填
  int movement_zone = 0;    // kStartZoneSel[MapType]
  int want_zone_type = 0;   // zoneType[movement_zone][largestZone]

  // 已经缩过一圈的可用矩形：{[0x87F8E4]+4, [0x87F8E8]+4, [0x87F8EC]-8, [0x87F8F0]-8}
  MapRect usable;

  StartPointContext() : grid(0, 0) {}
};

// 0x594420 GrowRegion(seed, movementZone, wantType)。
// 从 seed 起 LIFO 洪泛，把同一移动区类型的连通块并成一个 RegionClass。
// **不取随机数**。返回新建的区域。
RegionClass* GrowStartRegion(StartPointContext& ctx, const CellCoord& seed,
                             int movement_zone, int want_type);

// 0x594F40 ChooseWaypoints。纯确定性最远点采样，**不取随机数**。
// `ok` 为 false 表示原版在这里 `return NULL`（调用方随即整段放弃）。
std::vector<CellCoord> ChooseWaypoints(const StartPointContext& ctx,
                                       const std::vector<CellCoord>& cand,
                                       int start_count, bool* ok);

// 0x5A7250 AreaIsPassable(rect, allowPavedRoad, allowPavedRoadEnd)（__fastcall
// ecx = rect、dl = 第二个开关、一个栈参 ret 4）。
// 先拿矩形的**四个角**过菱形（坐标各自 movsx 截成 int16），再逐格过五条砖谓词。
// ⚠️ 矩形是它四个角的凸包、菱形是四个半平面的交，所以角过了则格格都在图内——
// 内层那个 0x5657A0 永远取不到共享哑元。
bool AreaIsPassable(const StartPointContext& ctx, const MapRect& rect,
                    bool allow_paved_road, bool allow_paved_road_end);

// 0x594870 PickPointsInRegion(region, base)。两个取数点。
void PickPointsInRegion(StartPointContext& ctx, RegionClass* r, int base);

// 第 0 步：重建寻路分区（0x56C510），把 movement_zone / want_zone_type 填进 ctx。
void ComputeStartZoneSelection(StartPointContext& ctx, ZoneTable& zones);

// 0x594B50 PlaceStartPoints。永远返回 true（原版所有出口都 return 1）。
bool PlaceStartPoints(StartPointContext& ctx);

// —— 下面两个是从 PlaceStartPoints 里拆出来的纯函数，方便单独对拍 ——

// 第 3 步的排序键与比较器（qsort 0x7C8B48 + 0x5AD9C0，按键升序）。
// `region_count` 是**销毁小区域之前**的区域个数。
int StartRegionSortKey(int cell_count, int region_id, int region_count);

// 第 4 步的名额分配。`cell_counts` 已按第 3 步排好序。
std::vector<int> AllocateStartQuota(const std::vector<int>& cell_counts,
                                    int total_players);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_START_POINTS_H_
