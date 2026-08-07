#include "make_grid.h"

#include "fast_math.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {

void MakeGrid(std::vector<Rect>* out, int n, const Rect& bounds,
              Randomizer& rng) {
  // 0x59A8F3-0x59A909：查表 FastSqrt 后立刻 _ftol（截断）。
  // ⚠️ 不要换成 std::sqrt：查表实现系统性偏小，n >= 16641 时结果会少 1，
  // 这个行为要照抄（这里 n <= 12，还在安全区，但写法必须一致）。
  const int r = Ftol(FastSqrt(static_cast<double>(n)));

  // 0x59A917：r*r == n 时列数就是 r，否则 r+1
  const int cols = (r * r == n) ? r : r + 1;
  // 0x59A933：保证 rows*cols >= n
  const int rows = (cols * r >= n) ? r : cols;

  // 0x59A956：两个尺寸**先都按 cols 除**，抛硬币之后才有一个改成按 rows 除。
  int cell_w = bounds.w / cols;
  int cell_h = bounds.h / cols;
  int inner_dx = 0, inner_dy = 0, outer_dx = 0, outer_dy = 0;

  // 0x59A97C-0x59A9A2：抛硬币决定网格朝向（等价于把网格转置）。
  // fcomp 0.5 之后 test ah,1 / je —— 取到的分支是「>= 0.5」那一路。
  const double coin =
      static_cast<double>(static_cast<std::int64_t>(rng.Random())) * kInvUintMax;
  if (coin >= 0.5) {
    cell_h = bounds.h / rows;      // cell_w 保持 = w/cols
    inner_dx = cell_w;             // 内层沿 X 走
    outer_dy = cell_h;
  } else {
    cell_w = bounds.w / rows;      // cell_h 保持 = h/cols
    inner_dy = cell_h;             // 内层沿 Y 走
    outer_dx = cell_w;
  }

  // 0x59A9D2：多出来的格子数
  int rem = rows * cols - n;

  std::vector<int> counts(static_cast<std::size_t>(rows < 0 ? 0 : rows), cols);
  std::vector<int> pool;
  pool.reserve(counts.size());
  for (int i = 0; i < rows; ++i) pool.push_back(i);

  // 0x59AAE3-0x59AB62：随机挑 rem 个外层行「少放一个」，不放回。
  for (; rem > 0; --rem) {
    const int j = PickBelowMul(rng, static_cast<int>(pool.size()));
    counts[static_cast<std::size_t>(pool[static_cast<std::size_t>(j)])] = r;
    pool.erase(pool.begin() + j);
  }

  // 0x59AB7A-0x59ACA7：铺格子。
  int x = bounds.x;
  int y = bounds.y;
  for (int i = 0; i < rows; ++i) {
    if (counts[static_cast<std::size_t>(i)] < cols) {
      // 0x59AB94/0x59ABA9：少放一个就把这一行整体推半格，让它居中。
      // cdq/sub/sar 是**向零取整**的除以 2，负数上与算术右移不同。
      if (inner_dx > 0) {
        x += (cell_w - (cell_w < 0 ? -1 : 0)) >> 1;
      } else {
        y += (cell_h - (cell_h < 0 ? -1 : 0)) >> 1;
      }
    }
    for (int k = counts[static_cast<std::size_t>(i)]; k > 0; --k) {
      // 0x59AC23：四周各内缩 2 格
      out->push_back(Rect{x + 2, y + 2, cell_w - 4, cell_h - 4});
      x += inner_dx;
      y += inner_dy;
    }
    x += outer_dx;
    y += outer_dy;
    // 0x59AC84：换行时把内层那根轴拉回起点
    if (inner_dx > 0) {
      x = bounds.x;
    } else {
      y = bounds.y;
    }
  }
}

}  // namespace rmg
}  // namespace ra2
