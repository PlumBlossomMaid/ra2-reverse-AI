#include "blob_region.h"

#include <algorithm>
#include <cstdlib>

#include "fast_math.h"

namespace ra2 {
namespace rmg {
namespace {

// 0x59B9A3 / 0x59B9BA 用的是 sar——负数向 −∞ 取整。fp_guard.h 里有
// static_assert((-2 >> 1) == -1) 保证本平台的 >> 就是算术右移。
inline int Sar1(int v) { return v >> 1; }

}  // namespace

bool InRegion(const CellCoord& p, const Rect& region, bool ellipse,
              double ellipse_a, double ellipse_b, const Rect& full_bounds,
              int map_width) {
  if (region == full_bounds) return true;   // 0x59BABD 起的四条 cmp

  if (!ellipse) {
    // 0x59BB83：等距坐标直接比，半开区间 [x, x+w) × [y, y+h)
    return region.x <= p.x && p.x < region.x + region.w &&
           region.y <= p.y && p.y < region.y + region.h;
  }

  // 0x59BAFA：等距逆变换。原版把 2*rx 与 2*ry 先凑成整数再 fild，然后 ×0.5，
  // 所以这里也保持"先整数、后乘 0.5"，不要直接写 rx。
  const int two_rx_rel = p.x - p.y + map_width - 1 - 2 * region.x;  // = 2*(rx - bx)
  const int two_ry_rel = p.x + p.y - map_width - 1 - 2 * region.y;  // = 2*(ry - by)
  const double dx = static_cast<double>(two_rx_rel) * 0.5 - static_cast<double>(region.w) * 0.5;
  const double dy = static_cast<double>(two_ry_rel) * 0.5 - static_cast<double>(region.h) * 0.5;
  return dx * dx * ellipse_a + dy * dy * ellipse_b < 1.0;   // 严格小于
}

double DirectionalCost(const CellCoord& p, const CellCoord& n, const Rect& region,
                       const CellCoord& target, float aspect_x, float aspect_y,
                       const Rect& full_bounds) {
  if (region == full_bounds) {
    // 0x59BA60：退化成 p 与 n 的欧氏距离
    const int dy = p.y - n.y, dx = p.x - n.x;
    return FastSqrt(static_cast<double>(dy * dy + dx * dx));
  }

  // 0x59B989 起：把 n 与 target 的差投到真实坐标的两个轴上。
  // (X−Y)>>1 ~ rx、(X+Y)>>1 ~ ry，公共的 W 项在相减时抵消掉。
  const int ax = std::abs(Sar1(n.x - n.y) - Sar1(target.x - target.y));
  const int ay = std::abs(Sar1(n.x + n.y) - Sar1(target.x + target.y));
  if (ax == 0 && ay == 0) return 0.0;   // 0x59B9D6 fld [0x7E1748] = 0.0f

  const double len = FastSqrt(static_cast<double>(ay * ay + ax * ax));
  // 0x59BA30：切比雪夫步距，注意是 p 与 n 之间，不是与 target
  const int cheb = std::max(std::abs(p.x - n.x), std::abs(p.y - n.y));

  const double first = static_cast<double>(ax) / len;   // 0x59BA12 fdiv
  const double second = static_cast<double>(ay) / len;  // 0x59BA3A fidivr
  const double sum = static_cast<double>(aspect_x) * first +
                     second * static_cast<double>(aspect_y);
  return static_cast<double>(cheb) * sum;
}

}  // namespace rmg
}  // namespace ra2
