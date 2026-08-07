// 阶段 9「Creating hills」的第 ③~⑥ 步：角点高度格网子系统。
//
// 造山不是直接改砖号，而是先在**格子的角点**上建一张整数高度格网（单位是像素，
// 15 像素 = 1 级），把高度场一级一级抬进去，最后再按四角的相对高度反查
// `RampTable` 得到砖号与坡面编号。这一整套东西就是地图编辑器的「抬升地形」，
// **一个随机数都不取**，但每一步都改地形字节，逐字节复刻绕不开。
//
// 取证（gamemd.exe）：
//   0x5A35F0 CreateHills 编排（① 0x5A33F0 ② 0x5A2F50 ③ 0x6B2A70 ④ 主循环
//            ⑤ 0x6B3850 ⑥ 2x2 收尾遍）
//   0x6B2A70 建格网      0x6B4100 读当前级   0x6B4240 选要动的角
//   0x6B3E60 抬/降一级（含撤销栈 0xB0B650）  0x6B3A80 坡面合法性（递归）
//   0x6B3850 格网落回砖号 / 坡面             0x6B2520 IsRaisable
//   格网全局：基址 0xB0B6EC，rows 0x8759A0，stride 0x8759A4，
//             origin 0x8759A8 / 0x8759AC（由 0x6B2A70 从 MapClass +0x124.. 拷来）
//   RampTable 0x83FF18（19 个 int[4]，循环上界硬编码 0x83FF64）
// 详见 docs/random-map/stage-hills.md §三~§九。
#ifndef RA2_RMG_CORNER_LATTICE_H_
#define RA2_RMG_CORNER_LATTICE_H_

#include <vector>

#include "blob_paint.h"     // PaintWorkCell：+0x08 height / +0x45 flag45
#include "fast_math.h"
#include "map_grid.h"
#include "place_tile.h"     // TileWorld
#include "theater.h"

namespace ra2 {
namespace rmg {

// 0x83FF18，19 项。单位是像素，除以 15 就是级数；四个分量按
// **北 / 东 / 南 / 西** 顺序，即格网坐标 (cx,cy) (cx+1,cy) (cx+1,cy+1) (cx,cy+1)。
// 第 0 项指向 .bss 的 0xB0B6DC（全 exe 无写入点，恒为 {0,0,0,0}）。
extern const int kRampTable[19][4];

// 格网的一个角点，原版是 8 字节。
struct CornerEntry {
  int height = 0;       // +0x00 像素高度
  bool locked = false;  // +0x04 这个角不许动（周围有不可抬升的格子）
  bool dirty = false;   // +0x05 本轮被改过，§八 只处理四角有 dirty 的格子
  // +0x06：`0x6B2A70` 分配时**只清 +0x00/+0x04/+0x05**，这个字节是未初始化的
  // 堆垃圾。它唯一的读点在 0x6B3AC6（`force != 0 && byte6 != 0` 时允许改动
  // 锁住的角），而 RMG 主路径的 force 恒为 0，所以读不到。这里定为 false 并
  // 保留字段，是为了让 force=1 的调用（地图编辑器那条路）也能对得上形状。
  bool byte6 = false;
};

// 撤销栈的元素，原版 12 字节（DynamicVectorClass at 0xB0B650，GrowthStep = 10）。
struct CornerUndo {
  int x = 0;
  int y = 0;
  int old_height = 0;
};

class CornerLattice {
 public:
  int stride = 0;    // [0x8759A4] = MapClass+0x12C + 1
  int rows = 0;      // [0x8759A0] = MapClass+0x130 + 1
  int origin_x = 0;  // [0x8759A8] = MapClass+0x124
  int origin_y = 0;  // [0x8759AC] = MapClass+0x128
  std::vector<CornerEntry> cells;
  std::vector<CornerUndo> undo;

  CornerEntry& At(int x, int y) {
    return cells[static_cast<std::size_t>(stride) * y + x];
  }
  const CornerEntry& At(int x, int y) const {
    return cells[static_cast<std::size_t>(stride) * y + x];
  }
  bool Allocated() const { return !cells.empty(); }
};

// 造山要碰的格子字段。`TileWorld` 已经给了 +0x38 / +0x11A 与哑元的一部分，
// 这里补上层高、坡面、overlay、object 与「砖可变形」查询。
//
// ⚠️ 哑元格子（0xABDC50）在 §九 收尾遍里会被**读也会被写**：地图边缘的坡块
// 往 (+1,+1) 方向探邻格时会探到菱形外，`GetCellAt` 返回同一份哑元，
// 于是它的 +0x11C 会被读、+0x38/+0x11A/+0x11B/+0x11C 会被写。必须提供一份
// 可写的哑元，否则边缘行为不同。
class HillWorld : public TileWorld {
 public:
  virtual int Level(const CellCoord& c) const = 0;    // +0x11B，取**有符号**字节
  virtual int Ramp(const CellCoord& c) const = 0;     // +0x11C
  virtual int Overlay(const CellCoord& c) const = 0;  // +0x44，IsRaisable 要求 == -1
  virtual bool HasObject(const CellCoord& c) const = 0;  // +0xE4 != 0 即不可抬升

  // [0xA8ED38] 砖表长度 + [0xA8ED2C] 砖表项 +0x2E0（疑似 Morphable）：
  //   if (tile < count && tile != 0xFFFF) return TileTypes[tile]->+0x2E0 != 0;
  //   return true;
  virtual bool TileMorphable(int tile) const = 0;

  // 共享哑元 0xABDC50 的可读写字段
  virtual CellCoord DummyCoord() const = 0;
  virtual int DummyLevel() const = 0;
  virtual int DummyRamp() const = 0;
  virtual void SetDummyLevel(int v) = 0;
  virtual void SetDummyRamp(int v) = 0;
};

struct HillContext {
  MapGrid grid;                                  // W = [0x87F8DC]，H = [0x87F8E0]
  std::vector<PaintWorkCell>* work = nullptr;    // [0xABED10]，side² 条
  HillWorld* world = nullptr;
  const TheaterData* theater = nullptr;
  int ruggedness = 0;                            // MapSeedClass +0x44
  NormalDeviate* nd = nullptr;
  CornerLattice lattice;

  HillContext() : grid(0, 0) {}
};

// 0x6B2520 IsRaisable(cell)。五条与门：在菱形内、没有 overlay（+0x44 == -1）、
// 格子上没有 object（+0xE4 == 0）、工作记录 +0x45 == 0（不是锚点）、砖可变形。
bool IsRaisable(const HillContext& ctx, const CellCoord& c);

// 0x6B2A70。重建整张格网：先按 (rows × stride) 全部清零，再逐角点算出初始高度
// 与 locked 标记。**不取随机数**。
void BuildCornerLattice(HillContext& ctx);

// 0x6B4100 GetCurrentLevel(cell)：四角最低的角除以 15；格子不可抬升时
// 直接返回它当前的 +0x11B。
int GetCurrentLevel(const HillContext& ctx, const CellCoord& c);

// 0x6B4240 PickCorners(cell, dir)：返回 4 位掩码，bit i = 第 i 个角（北东南西）。
//   四角等高 -> 所有未锁的角；dir > 0 -> 低于最高角的未锁角；否则 -> 高于最低角的。
int PickCorners(const HillContext& ctx, const CellCoord& c, int dir);

// 0x6B3A80 CheckSlopes(dir, force, x, y)：8 邻域内相邻角点高差不得超过 15，
// **会顺手把邻角拉动并递归**。返回 false = 这一级非法。
bool CheckSlopes(HillContext& ctx, int dir, bool force, int x, int y);

// 0x6B3E60 RaiseCorners(dir, force, xy, mask)：把 mask 选中的角抬/降一级。
// 返回值在 RMG 主路径上**被完全忽略**（失败时已整批回滚，驱动循环照样抬下一级）。
bool RaiseCorners(HillContext& ctx, int dir, bool force, const CellCoord& c,
                  int mask);

// 0x5A361A 起的第 ④ 步：按**线性下标**扫工作数组，把高度场一级一级抬进格网。
// 从 CreateHills 里拆出来，是为了让单测能在 ⑤ 销毁格网**之前**看一眼格网
// （原版的基准就是在 0x6B3850 入口处采的）。
void RaiseAllCells(HillContext& ctx);

// 0x6B3850。按格网刷砖与坡面，跑完销毁格网。
void ApplyLatticeToCells(HillContext& ctx);

// 0x5A36C6 起的 2x2 收尾遍：把两种「四格拼一个大坡」的形状压平回空地。
void FlattenRampBlocks(HillContext& ctx);

// 0x5A35F0 CreateHills()。整个阶段 9 的入口。
void CreateHills(HillContext& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_CORNER_LATTICE_H_
