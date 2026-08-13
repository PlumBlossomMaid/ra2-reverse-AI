// 悬崖相关的四组函数——阶段 4「Making regions」的收尾动作。
//
// 取证（gamemd.exe）：
//   0x5A19E0 悬崖直角处随机削一格 / 垫一格（2 个取数点）
//   0x578E60 全图重刷砖（两遍：0x579010 填坑 + 0x579620 悬崖自动选型）
//   0x579010 把一两格宽的坑抬起来（递归，不取随机数）
//   0x579620 按八邻域高差给悬崖自动选砖（**每格** 1 个 PickRange(0,5)）
//   0x5A17F0 多格悬崖砖重掷同形变体（内部走 0x5A1350 的 15 个取数点）
//   0x5A1350 在 CliffSet 的三连组里换一个**别的**成员（28↔29 对换不取数）
// 详见 docs/random-map/stage-making-regions.md §六 ~ §八。
#ifndef RA2_RMG_CLIFF_WORK_H_
#define RA2_RMG_CLIFF_WORK_H_

#include "making_regions.h"

namespace ra2 {
namespace rmg {

// 0x5A19E0 `TrimCliffCorners()`。全图迭代，对每一格取一张高差掩码（0x579B70），
// 三条互斥分支对应三个 90° 直角：0x83（N+NE+E）削 n2、0x38（S+SW+W）削 n6、
// 0xE0（W+NW+N）垫 cell。前两条各抽一次 PickBelow(2)，只有抽到 0 才动手；
// 0xE0 不抛硬币。第四个直角 0x0E（E+SE+S）没有分支（原版照缺）。
// 层高加减写在 byte 上会回绕；work 为空时层高照改、只不写归属。
void TrimCliffCorners(RegionContext& ctx, MakingWorld& world);

// 0x578E60 `RepaintAll(owner, level)`（调用点 0x598D73：`push -1; push ebx`；
// 这里 owner 传 -1、level 传 0。第二栈参才是 FillNotch/AutoCliff 的 owner，
// 原版实际传 0xFFFFFFFF = -1——emu 的 run_call 顺序压栈会把参数弄反、
// 得出 owner=0 的假象，golden 基准的 dump 脚本 reversed 压栈才是真实值）。
// 工作数组为空就 new（阶段 1 已建好，这里只在独立调用时触发）；
// 把每格工作记录 +0x4A 置 1；第一遍全图 FillNotch(-1)，第二遍只对
// 非坡（+0x11C == 0）且未定砖的格子调 AutoCliff(-1)，两遍都以 false 提前终止；
// 最后清掉贴砖器那对暂存全局（C++ 侧无对应物，注释留档）。
void RepaintAll(RegionContext& ctx, MakingWorld& world);

// 0x579010 `FillNotch(cell, owner)`。**不取随机数**。取高差掩码后按四组判据
// （两个相对正方向都高 / 对角对带例外 / 隔一个空当的八对 / 一格宽的夹缝）
// 判断本格是不是"一两格宽的坑"，是则 `Level += 4` 并写 owner，再向八邻递归。
// 返回 false 的唯一出口是 `o > 0 && o != owner && owner != -1`。原版 0x5792A9
// 的 packed 判定实际读的是调用者 param2（GetWorkOwner ret 4 后 esp 恢复、
// [esp+0x28] 偏移错位）→ 两个调用者（Rebuild 0x58EF49 / RepaintAll 0x578E60
// 的 param_2）都是 0xFFFFFFFF → 判据命中恒抬高；0x5792BA 的"blob==packed"
// 分支是死代码。
bool FillNotch(RegionContext& ctx, MakingWorld& world, const CellCoord& c,
               int owner);

// 0x579620 `AutoCliff(cell, owner)`。**入口每格取一个** `r = PickRange(0, 5)`，
// 同一个 r 被 `r % 3`（三选一）和 `r & 1`（二选一）双份消费。按高差掩码的
// 决策树选出 CliffSet 组内下标，贴到本格（落点偏移表 0xABDDA4 恒为 (0,0)，两层死）。
// 返回 false 会中断 0x578E60 的第二遍（`!placed && !ok` 两个都假才中断）。
bool AutoCliff(RegionContext& ctx, MakingWorld& world, const CellCoord& c,
               int owner);

// 0x5A1350 `RerollCliff(tile)`：在 CliffSet 的三连组里换一个**别的**成员。
// 六个三连组 {4,5,6} {8,9,10} {11,12,13} {14,15,16} {22,23,24} {34,35,36}
// 组内第 1 → 第 2/3、第 2 → 第 1/3、第 3 → 第 1/2；`rel ∈ {28,29}` 常数对换
// （不取随机数）；其余原样返回。⚠️ 永远不返回原来那块砖。
// 前五组内联（`fadd 1.0` 把下界顶上去）、第六组走外联 PickRange——
// 比例常数是预乘好的 `2/(2³²−1)`，与 PickRange 的 `span × 1/(2³²−1)` 两步
// 乘**不是同一条浮点路径**，不要互相替换。
int RerollCliff(Randomizer& rng, int tile, int cliff_set);

// 0x5A17F0 `RerollMultitileCliffs()`。全图迭代，找出"确实是一整块多格悬崖砖"
// （同砖号 + 南/东邻格子块号更小），把整块重掷成同形变体（StampTile，level=-1
// ⇒ 不改层高、但会重写 +0x11C 坡度）。原点由邻格子块号反推。
void RerollMultitileCliffs(RegionContext& ctx, MakingWorld& world);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_CLIFF_WORK_H_
