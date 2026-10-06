// 阶段 7「Adding tech buildings」——撒中立科技建筑（0x5A95B0）。
//
// `MapType == 0` 整段跳过；否则按 `MapType == 2` 分两条**互斥**的路：
//   A（MapType == 2）：抽 n = PickBelow(3)，把「有出生点的区域」整表刷 n 遍，
//                      每遍每区调一次 0x595400（阶段 6 那个 PlaceRandomBuilding）
//   B（其余）：抽 m = PickBelow(5) 栋，每栋最多 100 次尝试，每次尝试最多
//              200 次「摸格子」，摸中空地就按占地表逐格验，全过才落地
//
// 取证（gamemd.exe）：
//   0x5A95B0 AddTechBuildings   0x5A95BF/0x5A9645/0x5A96B8 三个取数点
//   0x598030 PickRange(lo,hi)（取数点 0x59805E，多阶段共用）
//   0x42D470 CellStruct(0,0)    0x50E470 CellStruct::operator==
//   0x5657A0 GetCellAt          0x486380 IsPlain
//   0x578540 IsWithinUsableArea(cell, 1)
//   0x5117D0 HouseTypeClass::FindIndexByName("Neutral")  0x502D30 找那一家
//   RulesClass [0x8871E0]：+0xAE0 数据 / +0xAEC 个数 = NeutralTechBuildings
// 详见 docs/random-map/stage-tech-buildings.md。
#ifndef RA2_RMG_TECH_BUILDINGS_H_
#define RA2_RMG_TECH_BUILDINGS_H_

#include <vector>
#include <string>

#include "blob_paint.h"   // PaintWorkCell：+0x45
#include "map_grid.h"
#include "randomizer.h"
#include "scaled_random.h"   // PickBelow / PickRange
#include "regions.h"

namespace ra2 {
namespace rmg {

// 占地表的一项（`BuildingTypeClass` 虚函数 `+0x90(1)` 返回的 short 对，
// 以 `(0x7FFF, 0x7FFF)` 收尾）。两个分量都是 **int16**，与锚点相加也按 int16。
struct FoundationOffset {
  int dx = 0;
  int dy = 0;
};

// 这一阶段要问外界的事。建筑对象本身是引擎的东西（`BuildingClass` 0x720 字节、
// 三个虚函数），复刻侧只用一个不透明句柄表示：`< 0` = `operator new` 失败。
class TechWorld {
 public:
  virtual ~TechWorld() {}

  // —— 格子 ——
  virtual int Level(const CellCoord& c) = 0;        // +0x11B，**有符号**字节
  virtual bool IsPlain(const CellCoord& c) = 0;     // 0x486380
  virtual bool HasOccupier(const CellCoord& c) = 0; // +0xE4 != 0
  virtual int Field0xEC(const CellCoord& c) = 0;    // +0xEC，== 3 即否决
  virtual bool WithinUsableArea(const CellCoord& c) = 0;   // 0x578540(cell, 1)

  // —— 建筑 ——
  // rules.ini [General] NeutralTechBuildings 的条数（`[0x8871E0]+0xAEC`）
  virtual int NeutralTechBuildingCount() = 0;
  // operator new(0x720)。⚠️ 它在**抽种类之前**（0x5A967D），返回 0 时那次
  // PickBelowMul 一个随机数都不花——分支顺序不能调换。
  virtual bool AllocBuildingMemory() = 0;
  // BuildingClass::BuildingClass(types[k], neutralOwner)（0x43B740）
  virtual int CreateBuilding(int type_index) = 0;
  // 虚函数 +0x90(1)：占地表
  virtual const std::vector<FoundationOffset>& Foundation(int handle) = 0;
  // 虚函数 +0xD8(&coord, 0)：落地。coord = (x*256+128, y*256+128, 0)
  virtual bool Unlimbo(int handle, const CellCoord& at) = 0;
  // 虚函数 +0x20(1)：没放下去就销毁
  virtual void Destroy(int handle) = 0;

  // 0x595400（分支 A 专用，与阶段 6 的是同一个）
  virtual void PlaceRandomBuilding(RegionClass* r) = 0;
};

struct TechTraceEntry {
  std::string type;
  int type_index = -1;
  int attempt = 0;
  int x = 0;
  int y = 0;
  bool ok = false;
  bool unlimbo = false;
};

struct TechContext {
  int side = 0;                                 // [0x89C2DC] = W + H + 1
  std::vector<PaintWorkCell>* work = nullptr;   // [0xABED10]，side² 条
  RegionSet* regions = nullptr;                 // [0xABDF94] / [0xABDFA0]
  Randomizer* rng = nullptr;
  TechWorld* world = nullptr;
  int map_type = 1;                             // MapSeedClass +0x3C
  const std::vector<std::string>* type_names = nullptr;
  std::vector<TechTraceEntry>* trace = nullptr;
};

// 0x5A95B0。`MapType == 0` 时调用方根本不调它（门禁在 0x5A9410 一带）。
void AddTechBuildings(TechContext& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_TECH_BUILDINGS_H_
