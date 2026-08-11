// 阶段 4「Making regions」——把阶段 3 切出来的区域连起来。
//
// 名字骗人：阶段 3 才是"切区域"，这里做的是造坡道、架桥、修悬崖。
// 只在 `MapType ∈ {3,4}` 下执行（0x598960 里 `RMG: Making regions` 日志之后
// 那五个调用），是全流水线**取数最密**的一段（45 个取数点）。
//
// 五步（调度在 0x598960，两条调试日志之间）：
//   1) RebuildAndSplitRegions 0x58EBC0  重算成员/包围盒；过大的区域切开
//   2) RebuildRegions         0x58EF10  推倒重建区域，再算邻区表、开通路
//   3) TrimCliffCorners       0x5A19E0  悬崖直角处随机削一格 / 垫一格
//   4) RepaintAll             0x578E60  全图重刷砖（坡道、LAT 边界）
//   5) RerollMultitileCliffs  0x5A17F0  多格悬崖砖重掷同形变体
//
// 45 个取数点的归属（rng-ledger.md 阶段 4 一节，全部已实锤）：
//   0x58C6F0 三项优先级 1（每候选格 1 次）/ 0x58C800 SeedRegion 1（共用）
//   0x58D620 切分 4 / 0x58F2C0 架桥 5 / 0x5905D0 开通路 3
//   0x590970 开一条坡道 8 / 直坡×4 各 1 / 0x5A1350 变体重掷 15
//   0x5A19E0 直角削垫 2 / 0x598000 UniformUnit 1 / 0x598030 PickRange 1（共用）
//
// 详见 docs/random-map/stage-making-regions.md。
#ifndef RA2_RMG_MAKING_REGIONS_H_
#define RA2_RMG_MAKING_REGIONS_H_

#include "place_tile.h"
#include "regions.h"

namespace ra2 {
namespace rmg {

// 阶段 4 要同时扮演区域算子（RegionWorld）与贴砖器（TileWorld）的世界，
// 外加桥面 overlay 字段与两个阶段 4 专属出口。
class MakingWorld : public RegionWorld, public TileWorld {
 public:
  // RegionWorld / TileWorld 各声明了一次，非限定名查找会二义，壳子消歧义。
  // 通过 RegionWorld 路径读（两条继承路径中取一条，字段实现一致）。
  int Tile(const CellCoord& c) const {
    return static_cast<const RegionWorld*>(this)->IsoTile(c);
  }
  void SetTile(const CellCoord& c, int v) {
    static_cast<RegionWorld*>(this)->SetIsoTile(c, v);
  }
  // +0x38 砖号。RegionWorld 与 TileWorld 各声明了一份同名虚函数，
  // 非限定名查找二义，壳子统一走 TileWorld 路径（字段实现一致）。
  int IsoTile(const CellCoord& c) const {
    return static_cast<const TileWorld*>(this)->IsoTile(c);
  }
  void SetIsoTile(const CellCoord& c, int v) {
    static_cast<TileWorld*>(this)->SetIsoTile(c, v);
  }
  void SetSubTile(const CellCoord& c, int v) {
    static_cast<TileWorld*>(this)->SetSubTile(c, v);
  }
  // +0x11B 层高。RegionWorld 读、TileWorld 写，壳子补齐两边的非限定名。
  int Level(const CellCoord& c) const {
    return static_cast<const RegionWorld*>(this)->Level(c);
  }
  void SetLevel(const CellCoord& c, int v) {
    static_cast<TileWorld*>(this)->SetLevel(c, v);
  }

  // CellClass +0x11C 坡道号。0x578460 的 use_height 路径与 0x5A7440 的地面
  // 同高检查都读它。
  virtual int Ramp(const CellCoord& c) const = 0;

  // CellClass +0x44 overlay 序号。桥面（0x58F2C0）写 0x4A..0x62 那组 LOBRDG，
  // 落脚区检查（0x5902C0）与小屋选址（0x5904B0）都先查它 == -1。
  virtual int Overlay(const CellCoord& c) const = 0;
  virtual void SetOverlay(const CellCoord& c, int v) = 0;
  // CellClass +0x11E overlay 子帧号。桥面存"第几行/第几列"（0..2）。
  virtual int OverlayData(const CellCoord& c) const = 0;
  virtual void SetOverlayData(const CellCoord& c, int v) = 0;

  // 按砖号取 TileType（StampTile / PlaceTile 需要几何）。对应 [0xA8ED2C]。
  virtual const TileType* TileByIndex(int index) const = 0;

  // 0x5904B0 的出口：在 (c) 盖一间 CABHUT——整条 RMG 流水线里唯一构造
  // 游戏对象的地方。落 .map 上是 [Structures] 的一条记录，不是砖数据。
  virtual void PlaceRepairHut(const CellCoord& c) = 0;
};

// 阶段 4 全流程（0x598960 那五个调用）。map_type 必须是 3 或 4（调用方判断）。
void MakeRegions(RegionContext& ctx, MakingWorld& world, int map_type);

// ── 五个子阶段，都可以单独对拍 ──

// 0x58EBC0 `MapSeedClass::RebuildAndSplitRegions()`。三步：
//   1) 清空每个区域的成员表与包围盒（bbox 预置 {9999,9999,0,0}）
//   2) **倒着**扫工作数组，把格子塞回各自的区域
//   3) 区域太大就切开（阈值 `_ftol(2·((RegionSize·0.005+0.05)·H)·W)`，
//      切成功则从区域表摘掉并 `goto restart` 重扫）
void RebuildAndSplitRegions(RegionContext& ctx);

// 0x58D620 `RegionClass::Split()`——把一个区域劈成几块。
// 五步：标记 → 优先级扩散剥一块（四方向）→ 连通块重建（八方向）→
// 给新区域定层高（含 <101 格并进最大邻区）→ 清理。4 个取数点：
//   0x58D782 剥走多少格 PickRange(n/8, n/3)
//   0x58D7CD 种子格 PickBelow(cells.count)
//   0x58D8BD 扩散基准角 Random()×2π/(2³²−1)
//   0x58E37D 新层高 PickBelow(candidateCount)
// 扩散每弹出一格还吃 NormalDeviate()×π/8（2 个随机数），每个候选格吃
// Priority 的 1 个噪声随机数。
bool SplitRegion(RegionContext& ctx, RegionClass* region);

// 0x58EF10 `MapSeedClass::RebuildRegions()`。先全图填坑（0x579010 第一遍），
// 再整批销毁区域、按连通块重建（SeedRegion，与阶段 3 共用取数点），
// 最后若全部成功：算邻区表（0x58F0C0）+ 开通路（0x5905D0）+ 清邻区表。
void RebuildRegions(RegionContext& ctx, MakingWorld& world);

// 0x58F0C0 `RegionClass::BuildNeighbourList()`。不取随机数。邻区表 = 8 邻域里
// 出现过、且 id != 自己的区域。顺带重数一遍自己的格子（排除 (0,0) 哨兵）。
void BuildNeighbourList(RegionContext& ctx, RegionClass* r);

// 0x5905D0 `RegionClass::MakeConnections()`——`Accessibility` 就在这生效。
//   陆地区域：对每一对高度不同的邻区，抽 `PickBelow(101)`，命中
//     `< Accessibility` 就再抽 `PickBelow(3)` 多开 1~2 条坡道；
//     每条坡道先抽边界格再调 BuildRamp，最多试 100 次。
//   水域区域：把隔水相望的两个同层陆地邻区配对，调 BuildBridge 架桥。
void MakeConnections(RegionContext& ctx, MakingWorld& world, RegionClass* r);

// ── 阶段 4 公共零件（供 ramps/cliff_work 复用） ──

// 0x579B70 八邻域「比我高 4 层」掩码。位定义与水掩码 0x57B210 完全一致
// （bit0=NE 1=E 2=SE 3=S 4=SW 5=W 6=NW 7=N），判据换成
// `邻格 Level == 本格 Level + 4` 且不是悬崖/坡道/桥/瀑布类砖（0x4863D0）。
// 工作记录 +0x4A 为 0、或本格不在菱形内，直接返回 0。
unsigned HigherMask(const RegionContext& ctx, MakingWorld& world,
                    const CellCoord& c);

// 0x58E5D0 `RegionClass::GetBorderCells()`——切分专用的那个副本。
// 与 0x58D410（regions.h 里的 GetBorderCells）不同：倒着走成员表，
// 8 邻域里有一格的 work.regionId 跟自己不同就收进来然后 break；
// 向量增长步长 max(cellCount/2, 10)。越界邻格直接跳过、不算异邻。
std::vector<CellCoord> SplitBorderCells(const RegionContext& ctx,
                                        const RegionClass& region);

// 0x58C6F0 三项优先级：`0.15·d + 1.5·a + 2.5·U(0,1)`（噪声权重最大）。
// ⚠️ 每算一次就吃一个随机数（噪声项），按候选格数计的开销。
// `a` 是方位角离 base_angle 的偏差：先 `Atan(-dy/dx)` 按象限补 π，
// 再 `fabs` 后 while 减 2π、超 π 折回 2π−a（顺序照抄，先 fabs 再模）。
float Priority(Randomizer& rng, const CellCoord& origin,
               const CellCoord& c, double base_angle);

// 0x5A1E50 `CountAtLeast(rect, regionId, need)`：数这个矩形里有几格
// `work[].regionId == id`，够 need 格立刻返回 true（提前退出，不数完）。
// 出界格跳过。work 为空时 owner 视为 -1。
bool CountAtLeast(const RegionContext& ctx, const Rect& r, int id, int need);

// 0x594010 `Clearance(rect, lowRegionId)`：矩形四角都要 IsWithinUsableArea，
// 且**每格**的 work.regionId 只能是本区域（ctx.this 由调用方传入）或目标区域，
// 且是未定砖（0x486380）。任一条不满足立刻返回 0。
bool Clearance(const RegionContext& ctx, MakingWorld& world, int owner,
               const Rect& r, int low_region_id);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_MAKING_REGIONS_H_
