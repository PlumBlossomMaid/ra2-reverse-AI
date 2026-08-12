#include "height_field.h"

#include <cstdint>

#include "cell_ref.h"      // kAdjacentDelta（0x89F688）
#include "scaled_random.h"

namespace ra2 {
namespace rmg {

double TruncNormal(NormalDeviate& nd, double mu, double sigma, double lo,
                   double hi) {
  // 回退重铺条件（0x5A3215 slope / 0x5A32ED height 两处相同）：
  // 分布整体落在 [lo,hi] 之外才重铺——(mu - sigma > hi || mu + sigma < lo)。
  // 汇编逐条核对（fcomp + test ah,0x41/1 的 C3/C0 位）：
  // · slope  0x5A321D 比较 (-sigma0) vs hi、0x5A322E 比较 sigma0 vs lo
  // · height 0x5A32F3 比较 (mu-slope) vs hi、0x5A3304 比较 (slope+mu) vs lo
  //   ——两者形式一致，不存在"没盖住两端就重铺"的另一种语义；上一版误以为
  //   height 用 (mu-sigma > lo || mu+sigma < hi)，导致每格触发回退、sigma
  //   放大到 2.0，高坡道泛滥（33-40 数量翻倍）。
  // 重铺动作（0x5A3239 / 0x5A3315）：sigma = (hi-lo)*0.5, mu = sigma+lo
  if (mu - sigma > hi || mu + sigma < lo) {
    sigma = (hi - lo) * 0.5;
    mu = sigma + lo;
  }
  double v;
  do {
    v = nd() * sigma + mu;
  } while (v < lo || v > hi);   // 闭区间接受
  return v;
}

double RoundHalfAwayFromZero(double h) {
  if (h < 0.0) return static_cast<double>(Ftol(h - 0.5));   // 0x5A33A9
  if (h > 0.0) return static_cast<double>(Ftol(h + 0.5));   // 0x5A33CB
  return h;                                                  // 恰好 0 原样
}

void MarkHeightAnchors(TileWorld& world, const MapGrid& grid,
                       const TheaterData& theater,
                       std::vector<PaintWorkCell>& work) {
  const int side = grid.side();
  grid.ForEachCell([&](const CellCoord& c) {
    const int tile = world.IsoTile(c);
    const int sub = world.SubTile(c);
    // 0x5A3411：先问是不是岸砖，不是才问受保护地形——两支互斥
    const bool shore = IsShoreTile(tile, theater.shore_pieces);
    if (!shore && !IsProtectedTerrainCell(theater, tile, sub)) return;

    for (int d = 0; d < 8; ++d) {
      // 邻格坐标是**按 int16 相加**的（0x5A3428 起两次 short 运算）
      const CellCoord n{
          static_cast<std::int16_t>(c.x + kAdjacentDelta[d & 7].x),
          static_cast<std::int16_t>(c.y + kAdjacentDelta[d & 7].y)};
      if (!grid.InMap(n)) continue;
      // 0x5657A0 取格子：菱形内必有实体格，哑元那条路走不到，故不模拟副作用
      if (!IsUndecidedTile(world.IsoTile(n))) continue;   // 0x486380
      PaintWorkCell& wc = work[static_cast<std::size_t>(n.y * side + n.x)];
      if (shore) wc.height = 0.5;      // +0x08 = 0x3FE0000000000000
      wc.flag45 = true;                // +0x45
      break;
    }
  });
}

void GenerateHeightField(const MapGrid& grid, std::vector<PaintWorkCell>& work,
                         int ruggedness, NormalDeviate& nd) {
  // 0x5A2F59 起：三个由 Ruggedness 派生的尺度
  const double rug = static_cast<double>(ruggedness);
  const double drift = rug * 0.0001 + 0.1;
  const double base_slope = rug * 0.0025;
  const double max_slope = rug * 0.005;
  const double sigma0 = 0.025;
  if (base_slope < sigma0) return;          // 0x5A2FA8，Ruggedness < 10 直接走人

  const int side = grid.side();
  const int n = side * side;
  for (int i = 0; i < n; ++i) {
    PaintWorkCell& w = work[static_cast<std::size_t>(i)];
    // 打包坐标为 (0,0) 的槽位（菱形外）整条跳过
    if (w.coord.x == 0 && w.coord.y == 0) continue;
    const int x = w.coord.x, y = w.coord.y;

    w.slope = 0.0;
    double diag = 0.0, grad = 0.0;
    if (grid.InMap(x - 1, y - 1)) {
      diag = work[static_cast<std::size_t>((y - 1) * side + (x - 1))].height;
    }

    if (grid.InMap(x - 1, y)) {             // 左邻
      const PaintWorkCell& l = work[static_cast<std::size_t>(y * side + (x - 1))];
      w.height += l.height;
      w.slope = l.slope;
      grad = l.height - diag;
    } else {
      w.slope = base_slope;
    }

    if (grid.InMap(x, y - 1)) {             // 上邻
      const PaintWorkCell& u = work[static_cast<std::size_t>((y - 1) * side + x)];
      w.height += u.height;
      w.slope += u.slope;
      grad += u.height - diag;
    } else {
      w.slope += base_slope;
    }

    w.height *= 0.5;
    w.slope *= 0.5;

    // 梯度只留符号，幅度换成固定的 drift（0x5A316C-0x5A31A9）
    // ——山脊沿一个方向延伸就是这么来的
    if (grad > 0.0) grad = drift;
    else if (grad < 0.0) grad = -drift;
    else grad = 0.0;

    if (w.flag45) {                         // 锚点
      if (w.height <= 0.0) w.height = 0.0;
      w.slope = 0.0025;                     // 立即数 0x3F647AE147AE147B
    }

    // 第一次抽样：更新 slope，钉在 [0, maxSlope]
    double lo = -w.slope;
    double hi = max_slope - w.slope;
    w.slope += TruncNormal(nd, 0.0, sigma0, lo, hi);       // 0x5A3258

    // 第二次抽样：更新 height，钉在 [-2, +2]；sigma 就是刚更新的 slope
    lo = -2.0 - w.height;
    hi = 2.0 - w.height;
    double mu = grad;
    if (mu < lo) mu = lo;
    if (mu > hi) mu = hi;
    w.height += TruncNormal(nd, mu, w.slope, lo, hi);     // 0x5A3334
  }

  // 0x5A3392 起：整表取整
  for (int i = 0; i < n; ++i) {
    PaintWorkCell& w = work[static_cast<std::size_t>(i)];
    w.height = RoundHalfAwayFromZero(w.height);
  }
}

}  // namespace rmg
}  // namespace ra2
