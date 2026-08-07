// 涂块的两个几何谓词/代价函数：
//   InRegion       @ 0x59BAB0  候选格是否落在投放区域内（矩形 / 内切椭圆）
//   DirectionalCost@ 0x59B940  优先级模式 ≠ 0 时的方向性代价
// 两个都决定陆地形状，详见 docs/random-map/stage-seeding-water.md §四。
#ifndef RA2_RMG_BLOB_REGION_H_
#define RA2_RMG_BLOB_REGION_H_

#include "map_grid.h"

namespace ra2 {
namespace rmg {

// 真实坐标下的矩形 {x, y, w, h}，即原版那个 int[4]。
struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool operator==(const Rect& o) const {
    return x == o.x && y == o.y && w == o.w && h == o.h;
  }
};

// 0x59BAB0。`full_bounds` 是全局 [0xABE2F0..FC]；区域与它**逐字段相等**时直接放行
// （0x59BABD 起四条 cmp），这是投放策略传"整张图"时的快捷路径。
//
// ⚠️ 两条分支的坐标口径不一致，已由模拟执行确认**行为如此**（意图未确认）：
//   - 矩形分支拿的是**等距**坐标 (X, Y) 直接与 rect 比（0x59BB83 起）
//   - 椭圆分支先做等距逆变换换算到**真实**坐标 (rx, ry)（0x59BAFA 起）
// 照抄，不要统一口径。
//
// ellipse_a / ellipse_b 由调用方给成 1/(w*0.5)² 与 1/(h*0.5)²；
// 判据是 dx²·a + dy²·b < 1.0（严格小于，0x59BB68 fcomp + test ah,1）。
// ⚠️ 必须在 FpuChopScope 作用域内调用。
bool InRegion(const CellCoord& p, const Rect& region, bool ellipse,
              double ellipse_a, double ellipse_b, const Rect& full_bounds,
              int map_width);

// 0x59B940。区域等于全图边界时退化成 p 与 n 的欧氏距离（用查表 FastSqrt）。
// 否则：把「n 到 target」的方向拆成真实坐标下的两个分量，做各向异性加权，
// 再乘上 p 与 n 的**切比雪夫**步距——效果是让这块陆地朝 target 方向拉长。
//
// ⚠️ 方向分量用的是**算术右移**：`(X ∓ Y) >> 1`。负数右移向 −∞ 取整，
// 与除以 2 向零取整**不同**，这个差别会实打实地改变代价值，照抄。
// ⚠️ aspect_x / aspect_y 在原版里是 **float**（0x59BA44/0x59BA48 都是 dword），
// 不是 double；加宽发生在乘法之前。
// ⚠️ 必须在 FpuChopScope 作用域内调用：RC=chop 与就近舍入在 400 例随机抽样里
// 有 370 例结果不同。
double DirectionalCost(const CellCoord& p, const CellCoord& n, const Rect& region,
                       const CellCoord& target, float aspect_x, float aspect_y,
                       const Rect& full_bounds);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_BLOB_REGION_H_
