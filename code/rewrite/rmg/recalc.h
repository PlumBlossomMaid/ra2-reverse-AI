// 阶段 5：绿地扩张 `FUN_0059B740` + 「Recalculating cell attributes」那一遍
//
// 主流程 `0x598960` 一贯是「先打日志、再干活」，这一段是唯一的例外：
//   0x598E18  mov ecx, esi          ← 白设的，0x59B740 不读 ecx
//   0x598E1A  call 0x59B740         ← **没有自己的阶段名**
//   0x598E1F  call 0x4068E0("RMG: Recalculating cell attributes")
//   0x598E31  MapClass::CellIterator_Reset / 0x578290 Next
//   0x598E48  CellClass::RecalcAttributes(-1)
// 于是「绿地扩张」被挂在了下一条日志名下。两件事毫无关系：
//   · `0x59B740` 是真正的地形算法，本阶段唯一的取数点 `0x59B842` 在它里面；
//   · `RecalcAttributes` 一个随机数都不取，是一遍**校验/清理**。
//
// 取证（gamemd.exe）：`memory/data/decomp/rmg_starting_points.txt`（0x59B740）、
// 反汇编 0x59B740-0x59B938 与 0x47D2B0-0x47DD61。
// 详见 docs/random-map/stage-recalc.md。
#ifndef RA2_RMG_RECALC_H_
#define RA2_RMG_RECALC_H_

#include "map_grid.h"
#include "randomizer.h"
#include "shore_smooth.h"
#include "theater.h"

namespace ra2 {
namespace rmg {

// ── 绿地扩张 `FUN_0059B740`（无参、无返回值，全镜像只有 0x598E1A 一处调用）──
//
// 先扫全图，把「绿地格」的四个**正**方向邻居里还没定砖的收进一个前沿表；
// 然后抽签 `count / 3`（上限 1000）次，每次从前沿里删一个、涂成 GreenTile、
// 再把它自己的四个正邻里没定砖的接着入队。
//
// ⚠️ 必须在 FpuChopScope 作用域内调用（取数点 0x59B862 走 _ftol）。
void SpreadGreen(SmoothWorld& world, const MapGrid& grid,
                 const TheaterData& theater, Randomizer& rng);

// ── `CellClass::RecalcAttributes(-1)`（0x47D2B0，`ret 4`）在 RMG 里的可见效果 ──
//
// 整个函数有 700 多条指令，但对**地形**（+0x38 砖号 / +0x11A 子块 / +0x11B 层高）
// 的写只有四条，而 RMG 的调用形态又把其中两条锁死了：
//
//   0x47D58D  砖号 >= [0xA8ED38]（已装载砖总数，有符号 `jl`）→ 砖号 = 0xFFFF
//   0x47D5E6  子块在 .tmp 里是空洞 → 砖号 = 0xFFFF、子块 = 0
//   0x47D94E  **栈参 != -1 时**把栈参当层高写进 +0x11B —— RMG 三处调用全传 -1，
//             所以层高一格都不改。这也顺带钉死了那个 `-1` 的语义：
//             它不是「不指定 theater」，而是「不覆盖层高」。
//
// 前提条件（都是实锤，不是假定）：
//   · CellClass +0x44（挂在这一格上的 overlay 序号）必须是 -1，否则走 0x47D306
//     那条完全不同的分支。阶段 1 的清格子循环把每格 +0x44 写成 -1
//     （`rmg_init_map.txt`：+0x11C=0 / +0x11B=4 / +0x38=0 / +0x11A=0 /
//     +0x44=-1 / +0x11E=0），而第一个写 +0x44 的是阶段 8 的矿
//     （`rmg_ore.txt` / `rmg_tiberium.txt` 里 `cell+0x44 = ...`）。
//     阶段 5 在阶段 8 之前，所以这一遍必然走 0x47D57B。
//   · 砖号 == 0xFFFF 时跳去 0x47DB22，那条分支不碰砖号/子块/层高。
//
// 另外它无条件重算 +0x11C 坡道类型（`0x5471B0`：`.tmp` 子块头的 +0x2A）。
//
// ⚠️ 0x47D5BA 那里有个「+0x11C 非 0 就再判一次子块存在性」的重试
// （`0x544C20(subtile, 1)` 比 `(subtile, 0)` 多一步「.tmp 还没装载就现装」）。
// theater 全部装载完的前提下两次调用同值，所以**重试不可观测**，这里不建模，
// 也因此不需要给 TileWorld 加一个 +0x11C 的读接口。
//
// tile_count = `[0xA8ED38]`。
void RecalcAttributes(SmoothWorld& world, const CellCoord& c, int tile_count);

// 0x598E31-0x598E4D：迭代器扫全图，逐格调上面那个。
void RecalcAttributesPass(SmoothWorld& world, const MapGrid& grid,
                          int tile_count);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_RECALC_H_
