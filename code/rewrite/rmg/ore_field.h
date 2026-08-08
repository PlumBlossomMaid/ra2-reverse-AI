// 阶段 8「Creating tiberium」——布矿 / 布宝石。
//
// 外层 0x5A23A0 按区域走：先给每个区域挑一个「特殊矿田」候选点，再把总矿量
// 按面积摊到各候选点上，最后给每个出生点门口再补一片。真正干活的是
// 0x5A28C0 CreateOreField——又一份「节点池 + 一基最小堆」的扩散，
// 但比涂块多了一层「播不下去就换地方重来」的外壳（最多 10 次）。
//
// 取证（gamemd.exe）：
//   0x5A23A0 PlaceTiberium（选参真值表、参考点、最近候选点、矿量公式、
//            正态抖动窗口 [-100,+100]、出生点补偿 (avg-mn)*15+500）
//   0x5A28C0 CreateOreField（取数点 0x5A2B8E / 0x5A2C18 / 0x5A2D79）
//   0x578460 IsWithinUsableArea（第二个参数 = use_height，见 §五）
//   0x4CAC40 FastSqrt   0x486380 未定砖   0x5657A0 GetCellAt
//   0x71BB90 TerrainClass::TerrainClass（"TIBTRE0%d" @ 0x82C090）
// 详见 docs/random-map/stage-tiberium.md。
#ifndef RA2_RMG_ORE_FIELD_H_
#define RA2_RMG_ORE_FIELD_H_

#include <vector>

#include "blob_paint.h"   // PaintWorkCell：+0x3C queued / +0x45 flag45
#include "fast_math.h"    // NormalDeviate
#include "map_grid.h"
#include "randomizer.h"
#include "regions.h"
#include "zones.h"        // MapRect / IsWithinUsableArea

namespace ra2 {
namespace rmg {

// 布矿要碰的格子字段。
//
// ⚠️ 下面每个访问器都必须按 0x5657A0 GetCellAt 的口径实现：拿不到实体格就退到
// 那份**全局共享的哑元** 0xABDC50，并把请求坐标写进它的 +0x24。哑元只有一份，
// 上一次留下的 +0x11B / +0x11C / +0x44 会被下一次读到——0x578460 在
// `use_height` 为真时就会读哑元的层高与坡面（0x5784A2/0x5784A8），
// 这条路在地图边缘真的走得到。算法这边不再复述，交给实现方。
class OreWorld {
 public:
  virtual ~OreWorld() {}

  virtual int IsoTile(const CellCoord& c) = 0;          // +0x38
  virtual int Level(const CellCoord& c) = 0;            // +0x11B，**有符号**字节
  virtual int Ramp(const CellCoord& c) = 0;             // +0x11C
  virtual int Overlay(const CellCoord& c) = 0;          // +0x44，-1 = 没有 overlay
  virtual void SetOverlay(const CellCoord& c, int v) = 0;
  virtual int OverlayData(const CellCoord& c) = 0;      // +0x11E，**无符号**字节
  virtual void SetOverlayData(const CellCoord& c, int v) = 0;

  // CellClass::GetContainedTiberiumIndex。这里只用到 `!= -1`，
  // 返回值口径本身**未确认**。
  virtual int ContainedTiberiumIndex(const CellCoord& c) = 0;

  // 0x5A2BB8-0x5A2BE1：`new(0xE0)` + TerrainClass::TerrainClass(
  //   TerrainTypes[FindIndex(name)], &origin)。name 恒为 "TIBTRE01".."TIBTRE03"。
  virtual void CreateTerrain(const char* name, const CellCoord& at) = 0;

  // ScenarioClass::GetWaypointCoords（0x68BCC0）：`*out = this->Waypoints[idx]`
  virtual CellCoord Waypoint(int index) = 0;
};

struct OreContext {
  MapGrid grid{0, 0};
  std::vector<PaintWorkCell>* work = nullptr;   // [0xABED10]，side² 条
  OreWorld* world = nullptr;
  Randomizer* rng = nullptr;                    // [0xABE890]
  NormalDeviate* normal = nullptr;              // [0xABDFB8]，缓存跨阶段串
  int side = 0;                                 // [0x89C2DC] = W + H + 1

  // 0x578460 的两个入参来源：MapClass +0xF4（整图宽）与 +0x124.. 的可用矩形。
  int map_w = 0;
  MapRect local;
};

// MapSeedClass 里这一阶段读到的全部字段。按 CLAUDE.md 的数据驱动要求，
// 后两项必须从 rules.ini `[General]` 注入，不许写死。
struct TiberiumParams {
  int map_type = 1;          // +0x3C
  int resources = 1;         // +0x40
  int tiberium = 50;         // +0x54，当百分比用
  int min_tiberium = 2500;   // +0x2BC RMGMinimumTiberium
  int max_tiberium = 5500;   // +0x2C0 RMGMaximumTiberium
};

// 0x5A28C0 CreateOreField(seed, count, fieldId, isGems)。
// `count` 是**要铺满的格数**；`fieldId` 用来给工作数组 +0x3C 打「本田已入队」。
//
// ⚠️ 调用点还会 `mov ecx, MapSeedClass*`，但函数体一次都没读 ecx——无用赋值，
// 这里不带这个参数。
void CreateOreField(OreContext& ctx, const CellCoord& seed, int count,
                    int field_id, bool is_gems);

// 0x5A23A0 PlaceTiberium(MapSeedClass*)。逐区域布矿。
void PlaceTiberium(OreContext& ctx, RegionSet& regions,
                   const TiberiumParams& params);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_ORE_FIELD_H_
