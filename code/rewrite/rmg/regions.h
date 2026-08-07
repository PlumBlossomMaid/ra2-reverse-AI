// 阶段 3「Init regions」——把地图切成连通区域。
//
// 这一阶段自己只抽 1 个随机数，而且只在一条冷门分支上抽。它真正的价值是
// 建立「区域」这个概念：后面的出生点、矿、山丘、坡道、架桥全挂在区域上。
//
// 四步（调度在 0x598960，两条调试日志之间）：
//   1) 拆掉上一轮的 RegionClass（0x5AC290），[0xABED14] 归零，
//      工作数组每条记录的 +0x38 / +0x3C 全写 -1
//   2) CreateRegions   0x58CF90  扫工作数组，对每个未归属的水/绿格起一次种
//   3) 对每个 **wet** 区域（+0x14 != 0）先 Grow（0x58E740，4/5/6 圈）
//      再 FloodRest（0x58E9B0）
//   4) Finalize        0x58D010  剩下没归属的格子再兜一遍，并置 +0x1A = 0
//
// ⚠️ 第 3 步的过滤条件是 `region->+0x14 != 0`（0x598CAD `cmp byte [esi+0x14], bl`
// 且此处 bl == 0，0x598CB0 `je` 跳过本区域），而生效的 +0x14 是
// `IsWaterOrShore(seed) || IsGreen(seed)`——与 CreateRegions 起种的谓词**完全同一条**。
// 于是 CreateRegions 产出的每个区域都必然 wet == true，第 3 步一个不落。
//
// 由此还实锤一条结构性事实：SeedRegion 里那三条「不足 75 格就并掉」的分支
// （包含本阶段唯一的取数点 0x58CAF9）**只可能从第 4 步 Finalize 进去**，
// 因为只有 Finalize 会拿不满足上述谓词的格子起种。而取数点还额外要求
// `id == 0`，即 CreateRegions 一个区域都没建出来。这解释了总账里
// 「1 个取数点、只在一条冷门分支上」的说法。
//
// 取证（gamemd.exe）：
//   调度 0x598C60-0x598D01（rings = (cellCount>8000) + !bigMap + 4，
//     0x598CCC-0x598CDD；0x598CFA `xor cl,cl` → Finalize(0)）
//   CreateRegions 0x58CF90 / SeedRegion 0x58C800（唯一取数点 0x58CAF9）
//   Grow 0x58E740（__thiscall，ecx = region）/ FloodRest 0x58E9B0（__fastcall）
//   Finalize 0x58D010 / 边界格 0x5A0700 与 0x58D410 / 销毁 0x5AC290
//   谓词 0x4865D0 判水岸、0x4867B0 判绿、0x486380 判未定砖
// 详见 docs/random-map/stage-init-regions.md。
#ifndef RA2_RMG_REGIONS_H_
#define RA2_RMG_REGIONS_H_

#include <memory>
#include <vector>

#include "blob_paint.h"   // PaintWorkCell：+0x38 / +0x3C / +0x4B 是同一片内存
#include "map_grid.h"
#include "randomizer.h"
#include "theater.h"

namespace ra2 {
namespace rmg {

// ── 两个砖号谓词。都是纯函数，只看砖号与 theater 的组起始号 ──
//
// ⚠️ 两者都**不检查 -1**（该 theater 未定义这一组时全局就是 -1）。于是
// `tile == 0` 遇上未定义的组会被判成"落在组内"（0 >= -1 且 0 < -1+width）。
// 这是原版行为，照抄，不要加 -1 保护。取证：0x4865D0 与 0x4867B0 通篇没有
// 一条 `cmp ...,-1`，对比 0x4863D0 每个全局都先查 -1。

// 0x4865D0：这一格是水或岸（岸砖 42 块 / 水砖 14 块 / 四组瀑布各 4 块）
bool IsWaterOrShoreTile(int tile, const TheaterData& t);

// 0x4867B0：这一格是绿地（GreenTile 本身，或 ClearToGreenLat 的 16 块过渡砖）
bool IsGreenTile(int tile, const TheaterData& t);

// RegionClass，0x50 字节，构造 0x58BF70。字段注释里的偏移是原版布局。
struct RegionClass {
  // +0x00 候选点向量。阶段 6 填出生点候选、阶段 8 布矿时用。
  // ⚠️ 销毁（0x5AC290）只析构这一个向量，不碰 +0x04。
  std::vector<CellCoord> candidates;
  // +0x04 邻区编号表。只在阶段 4 存活（0x58F0C0 建、0x5905D0 用、当场销毁）。
  std::vector<int> neighbours;
  int id = 0;                 // +0x08 区域编号，取自 [0xABED14] 后自增
  int cell_count = 0;         // +0x0C 成员格数（构造后由 0x58CE2E 那段数出来）
  int level = 0;              // +0x10 本区域的高度层 = 种子格的 +0x11B
  // +0x14。⚠️ 构造 0x58BF70 只写 IsWaterOrShore(seed)，但调用方 0x58CE24
  // 立刻用 `IsWaterOrShore(seed) || IsGreen(seed)` 覆盖掉。后者才是生效值。
  bool wet = false;
  CellCoord seed;             // +0x16 种子格坐标
  bool finalized = false;     // +0x1A 「已定型，不再切分」；Finalize 写它的参数
  bool connected = true;      // +0x1B 疑似「与外界连通」；只见写不见读（未确认）
  int split_count = 0;        // +0x1C 被 0x58D620 切过几次；没找到读它的地方
  int start_points = 0;       // +0x20 分到的出生点个数（阶段 6 写）
  bool discarded = false;     // +0x24 「已作废」（0x58D620 并掉碎块后置 1）
  std::vector<CellCoord> cells;  // +0x28 本区域的格子表（阶段 4 填）
};

// 全局区域表：数据 [0xABDF94]、个数 [0xABDFA0]、编号计数器 [0xABED14]。
//
// 原版的对象是 `operator new` 出来的裸指针，销毁 0x5AC290 会把自己从表里
// 摘掉（后面的元素整体前移、个数 -1）再 free。这里用 pool 持有所有权，
// `list` 保持与原版表逐位一致的**顺序与个数**。
class RegionSet {
 public:
  // 0x58BF70：构造并登记。会把 next_id 自增。
  RegionClass* Create(const CellCoord& seed, int level, bool wet);

  // 0x5AC290(deallocate=1)：析构 +0x00、从 list 里摘掉、释放 +0x2C、free(this)。
  void Destroy(RegionClass* r);

  // 0x598960 的重置段：倒序销毁全部区域，再把编号计数器归零。
  void Reset();

  // 原版 `FindRegionById`：⚠️ 找不到会返回空指针而调用方直接解引用（未确认）。
  RegionClass* ById(int id);

  int Count() const { return static_cast<int>(list_.size()); }
  RegionClass* At(int i) const { return list_[static_cast<std::size_t>(i)]; }
  int next_id() const { return next_id_; }
  void set_next_id(int v) { next_id_ = v; }

 private:
  std::vector<std::unique_ptr<RegionClass>> pool_;
  std::vector<RegionClass*> list_;   // [0xABDF94] / [0xABDFA0]
  int next_id_ = 0;                  // [0xABED14]
};

// 区域阶段要碰的格子状态。
//
// ⚠️ 「哑元格子」：原版拿不到真实格子指针时会退到静态对象 0xABDC50，并把
// 请求坐标写进它的 +0x24（即 [0xABDC74]）。下面每个访问器在 `CellExists`
// 为假时都**必须落到那一份共享哑元上**——不是返回哨兵值、也不是无操作。
// 上一次留在哑元里的 +0x38 / +0x11B 会被下一次读到，这是能改变产物的行为。
class RegionWorld {
 public:
  virtual ~RegionWorld() = default;

  // 格子指针表 [0x87F924]（= MapClass +0x13C）里这一格是不是非空
  virtual bool CellExists(const CellCoord& c) const = 0;
  // 0x5657A0 的副作用：拿不到真实格子时把请求坐标写进 [0xABDC74]
  virtual void SetDummyCoord(const CellCoord& c) = 0;

  virtual int IsoTile(const CellCoord& c) const = 0;        // +0x38
  virtual void SetIsoTile(const CellCoord& c, int v) = 0;
  virtual void SetSubTile(const CellCoord& c, int v) = 0;    // +0x11A
  // +0x11B。⚠️ 原版是**有符号字节**（`movsx` / `cmp cVar,-1`），
  // 实现方必须按 int8_t 截断存储、按有符号读出。
  virtual int Level(const CellCoord& c) const = 0;
  virtual void SetLevel(const CellCoord& c, int v) = 0;
};

// 阶段 3 跑起来需要的全部环境。
struct RegionContext {
  MapGrid grid{0, 0};
  std::vector<PaintWorkCell>* work = nullptr;   // [0xABED10]，side² 条
  RegionWorld* world = nullptr;
  const TheaterData* theater = nullptr;
  Randomizer* rng = nullptr;
  RegionSet* regions = nullptr;

  // MapSeedClass +0x180 / +0x184 = 地图真实宽 W / 高 H（写入点 0x599700/0x599748）。
  // ⚠️ 不是装饰性字段：0x5A0700 把候选向量的**增长步长**设成 H*W，而
  // DynamicVectorClass 在步长 <= 0 时 push 会静默失败。W 或 H 为 0 时
  // CollectBorderCells 恒返回空表，这个行为必须能复现。
  int map_w = 0;
  int map_h = 0;

  // [0xABE2E4]：并区时邻格 Level 为 -1 的兜底值（来源未确认）
  int fallback_level = 0;
};

// ── 三个可以单独对拍的零件 ──

// 0x5A0700 `MapSeedClass::CollectBorderCells(owner)`（__thiscall，一个栈参）。
// 扫**工作数组**（下标 0..side²-1，每条 0x50 字节），挑出 `+0x38 == owner`
// 且 8 邻域里至少有一个**界内**邻格的 owner 不同的格子。
//
// ⚠️ 三个必须照抄的细节：
//   1) 坐标取的是记录自己存的 +0x00，再用它反算下标；不是用循环下标反推坐标
//   2) 打包坐标为 (0,0) 的记录整条跳过（0x5A073C 的 `sVar8 != 0 || sStack_6 != 0`）
//   3) 界外邻格**不算**"不同"——贴着菱形边的格子不会因此白捡一个边界身份
//   4) 命中第一个不同的邻格就 `break`，一格只入表一次
std::vector<CellCoord> CollectBorderCells(const RegionContext& ctx, int owner);

// 0x58D410 `RegionClass::GetBorderCells()`（__fastcall，ecx = region）。
// 和上面同义，但有四处不同，一处都不能省：
//   1) 走**地图格子迭代器**（0x578350 复位 + 0x578290 步进），顺序不一样
//   2) **不 break**——8 个邻格全看完才决定，所以每格都会走满 8 次取邻格
//   3) 取邻格用的是 `CellClass::GetNeighbourCell`（0x481810，**带查表**），
//      而且**先取格再判界内**。于是每一个界外邻格都会往共享哑元的 +0x24
//      写一次坐标；这是能被后续读到的可观测副作用，必须照做。
//      对比 0x58C800 / 0x58E740 / 0x58E9B0：它们用纯算术的 0x42D510，
//      只有过了界内判定才 GetCellAt。
//   4) 向量增长步长留在构造时的 10（不像 0x5A0700 会被改写成 H*W）
std::vector<CellCoord> GetBorderCells(RegionContext& ctx,
                                      const RegionClass& region);

// 0x58C800 `SeedRegion(seed)`（__fastcall，ecx = 种子格）。
// 从种子格 DFS（后进先出）涂一块同层同"湿度"的连通区；
// 不足 75 格（`visited > 0x4A` 才算数）就按三条互斥规则并掉。
// 返回新建的区域；走并区/重置分支时返回 nullptr（且 **不** 消耗编号）。
//
// ⚠️ 唯一的取数点在"不足 75 格 + 干的 + id == 0"这一条分支上（0x58CAF9）。
RegionClass* SeedRegion(RegionContext& ctx, const CellCoord& seed);

// 0x58CF90 `CreateRegions()`：扫工作数组，对每个 +0x38 == -1、打包坐标非 (0,0)
// 且「是水岸或是绿地」的格子起一次种。
void CreateRegions(RegionContext& ctx);

// 0x58E740 `RegionClass::Grow(rings)`（__thiscall）。从本区域的边界格出发，
// 一圈一圈把**同层**、未归属、且「未定砖或绿地」的邻格并进来，并把它们的
// Level 压成本区域的 level。
//
// ⚠️ 返回 false 的条件很宽：任何一个同层邻格既不属于本区域、又不满足
// 「未归属 + (未定砖或绿地)」，整个 Grow 立刻放弃。调度方 0x598CE4 **忽略**
// 这个返回值，照样接着调 FloodRest——所以它只是提前收工，不是错误。
bool Grow(RegionContext& ctx, RegionClass& region, int rings);

// 0x58E9B0 `RegionClass::FloodRest()`（__fastcall）。从边界格出发无限漫灌，
// 把所有未归属、且工作记录 +0x4B 非 0 的格子并进本区域。不改 Level，不看砖。
void FloodRest(RegionContext& ctx, RegionClass& region);

// 0x58D010 `Finalize(flag)`（__fastcall，ecx = flag）。剩下 +0x38 == -1 且
// 打包坐标非 (0,0) 的格子，**不经任何谓词过滤**，逐个再起一次种；
// 起成功的区域把 +0x1A 置成 flag。调度方传 0（0x598CFA `xor cl,cl`）。
void Finalize(RegionContext& ctx, bool flag);

// 阶段 3 全流程（0x598960 那一段）。map_type 只用来算 Grow 的圈数。
void InitRegions(RegionContext& ctx, int map_type);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_REGIONS_H_
