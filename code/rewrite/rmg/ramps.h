// 坡道与架桥——阶段 4「Making regions」把区域连起来的两种方式。
//
// 取证（gamemd.exe）：
//   开一条坡道 0x590970（8 个取数点，含邻域探测 0x590FD0 与 0x5A1E50）
//   四个直坡   0x5910F0 南 / 0x591740 东 / 0x591D80 北 / 0x592440 西（各 1 个）
//   三个转角   0x593AF0 N+E / 0x593550 E+S / 0x593030 S+W（0 个；第 4 条分支死）
//   架桥       0x58F2C0（5 个取数点）+ 0x5902C0 落脚区 + 0x5904B0 小屋 + 0x5A7440 地面同高
// 详见 docs/random-map/stage-making-regions.md §五之二 ~ §五之四。
#ifndef RA2_RMG_RAMPS_H_
#define RA2_RMG_RAMPS_H_

#include "making_regions.h"

namespace ra2 {
namespace rmg {

// 0x590970 `BuildRamp(hi, cell, lowRegionId, tol)`（__thiscall，ecx = 高的一侧区域）。
// `tol` 有两处独立作用：① 邻域探测的 `need = _ftol(lerp(5, 15, 1 − tol))`
// （第 0 次要 15/25 = 60%，第 99 次只要 5/25 = 20%）；② `tol > 0.5f` 时
// 追加四次"硬开长坡"（跨度固定 ±4、两端不抖动）。
// 八条主分支**不互斥**（建成即收尾），一次调用消耗 0~10 个随机数。
bool BuildRamp(RegionContext& ctx, MakingWorld& world, RegionClass* hi,
               const CellCoord& c, int low_region_id, float tol);

// ── 七个建造函数（0x5910F0/0x591740/0x591D80/0x592440、0x593AF0/0x593550/
//    0x593030）── 供 test_ramps 逐字节对拍直接链接（golden：dump_ramps_golden.py）。
// 签名统一：`(ctx, world, hi, a, b, low_region_id)`，hi 是层高大的区域。
// 直坡各 0/1 个取数点（dx/dy 非零才取硬币），转角 0 个。
bool BuildStraightSouth(RegionContext& ctx, MakingWorld& world,
                        RegionClass* hi, const CellCoord& a,
                        const CellCoord& b, int low_id);
bool BuildStraightEast(RegionContext& ctx, MakingWorld& world,
                       RegionClass* hi, const CellCoord& a,
                       const CellCoord& b, int low_id);
bool BuildStraightNorth(RegionContext& ctx, MakingWorld& world,
                        RegionClass* hi, const CellCoord& a,
                        const CellCoord& b, int low_id);
bool BuildStraightWest(RegionContext& ctx, MakingWorld& world,
                       RegionClass* hi, const CellCoord& a,
                       const CellCoord& b, int low_id);
bool BuildCornerNE(RegionContext& ctx, MakingWorld& world, RegionClass* hi,
                   const CellCoord& a, const CellCoord& b, int low_id);
bool BuildCornerES(RegionContext& ctx, MakingWorld& world, RegionClass* hi,
                   const CellCoord& a, const CellCoord& b, int low_id);
bool BuildCornerSW(RegionContext& ctx, MakingWorld& world, RegionClass* hi,
                   const CellCoord& a, const CellCoord& b, int low_id);

// 0x58F2C0 `BuildBridge(water, a, b)`（__thiscall，ecx = 水区域）。
// 在外壳最多 200 次重试里：抽一格水面当锚点（三重拒绝采样，取数点 0x58F342）
// → 四条探条找对岸（成对短路）→ 两端必须正好是 A、B → 跨距上限 `tries/25 + 8`
// → 铺桥面（LOBRDG overlay，不取随机数）→ 两端桥头路砖（各 1 个 PickBelow(2)）
// → 盖 CABHUT。一次成功架桥至少消耗 3 个随机数（锚点拒绝会更多）。
bool BuildBridge(RegionContext& ctx, MakingWorld& world, RegionClass* water,
                 RegionClass* a, RegionClass* b);

// 0x5902C0 桥的落脚区检查：以 rect 为左上角的 (W+1)×(H+1) **闭区间**，
// 锚格 = rect 左上角那格（0x5902eb 取 +0x11B 当基准）；每格要
// `Overlay(+0x44) == -1`、`Level` 等于锚格、且是未定砖或水岸。
bool BridgeFootprintClear(const RegionContext& ctx, MakingWorld& world,
                          const Rect& r);

// 0x5904B0 桥梁维修小屋选址：在 (W+1)×(H+1) 闭区间里找第一格
// 「没有 overlay、是平地（0x486380）、+0xE4 == 0」，找到就经
// MakingWorld::PlaceRepairHut 出口记录。整条 RMG 里唯一构造游戏对象的地方。
bool PlaceRepairHut(const RegionContext& ctx, MakingWorld& world,
                    const Rect& r);

// 0x5A7440 桥头路砖的地面同高检查：AreaIsPassable（0x5A7250，两个开关传
// allow_road 与 false）**再加**「每格的 +0x11B 都等于左上角那格」。
bool AreaSameLevel(const RegionContext& ctx, MakingWorld& world,
                   const Rect& r, bool allow_paved_road);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_RAMPS_H_
