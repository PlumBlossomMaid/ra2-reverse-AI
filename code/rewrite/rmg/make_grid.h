// 区域网格 `FUN_0059A8F0`——把可用区域切成 n 个矩形投放区。
//
// MapType 0 用它决定"在哪几块地方各画一坨陆地"，所以它直接决定了
// 大陆的分布骨架。取数 1 + rem 个（rem = rows*cols - n）。
//
// 取证（gamemd.exe）：0x59A8F0-0x59AD08（__stdcall，ret 0xC）。
// 详见 docs/random-map/stage-seeding-water.md §三。
#ifndef RA2_RMG_MAKE_GRID_H_
#define RA2_RMG_MAKE_GRID_H_

#include <vector>

#include "blob_region.h"
#include "randomizer.h"

namespace ra2 {
namespace rmg {

// 产出的记录数**恰好是 n**（证明见文档 §三）。
// ⚠️ 必须在 FpuChopScope 作用域内调用：内部用查表 FastSqrt + _ftol。
void MakeGrid(std::vector<Rect>* out, int n, const Rect& bounds,
              Randomizer& rng);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_MAKE_GRID_H_
