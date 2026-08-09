#include "place_blobs.h"

#include <cstdint>
#include <vector>

#include "make_grid.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

// 0x42B1F0：覆盖率的分母。**不是**菱形里的真实格数，高度多算了 4 行。
int UsableCellCount(const MapGrid& grid) {
  return (grid.height() + 4) * grid.width() * 2;
}

// 三个策略开头都做这件事：把上一阶段留下的入堆标记全清掉。
// 走的是全图迭代器（0x578350/0x578290），只碰菱形内的格子。
void ClearQueuedMarks(PaintContext& ctx) {
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    (*ctx.work)[ctx.grid.WorkIndex(c)].queued = 0;
  });
}

// 真实坐标矩形的中心 -> 等距坐标（0x59AE9E-0x59AECD，0x59B3C9-0x59B3F9 同式）。
// 两处除以 2 都是 cdq/sub/sar，向零取整。
CellCoord RectCentreToIso(const Rect& rc, int map_width) {
  const int hx = rc.w / 2;
  const int hy = rc.h / 2;
  return CellCoord{rc.x + hx + rc.y + hy + 1,
                   map_width + rc.y + hy - rc.x - hx};
}

int Abs(int v) { return v < 0 ? -v : v; }

}  // namespace

void PlaceBlobsGrid(PaintContext& ctx, const PlaceParams& p) {
  *ctx.blob_stamp = 1;

  // n = NumPlayers + PickRange(1, max(2, NumPlayers/2))
  // ⚠️ 下界是 1 不是 0：0x59AD8E 那句 fadd 1.0 把区间整体抬了一格。
  int span = p.num_players / 2;
  if (span < 2) span = 2;
  const int n = p.num_players + PickRange(*ctx.rng, 1, span);

  std::vector<Rect> regions;
  MakeGrid(&regions, n, p.usable, *ctx.rng);

  ClearQueuedMarks(ctx);

  // 原版是 DynamicVectorClass::Delete(0)——每次取头一个再整体前移。
  // 顺序等价于从前往后遍历，且不消耗随机数。
  for (const Rect& rc : regions) {
    // 预算 = 区域格数的 45%~50%。真实坐标 w×h 的矩形在等距网格里占 2wh 格。
    const double u =
        static_cast<double>(static_cast<std::int64_t>(ctx.rng->Random())) *
        kInvUintMax;
    const int budget =
        Ftol((u * 0.05 + 0.45) * static_cast<double>(2 * rc.h * rc.w));

    const CellCoord centre = RectCentreToIso(rc, ctx.grid.width());

    PaintParams pp;
    pp.budget = budget;
    pp.region = rc;
    pp.start = centre;
    pp.ellipse = true;
    pp.target = centre;
    pp.walk_sigma = 0.25;
    pp.directional = false;

    for (int k = 0; k < 10; ++k) {
      const int painted = PaintBlob(ctx, pp);
      // ⚠️ 失败才自增——块编号会跳号。成功时由涂块内部推进。
      if (painted == 0) ++*ctx.blob_stamp;
      if (painted != 0) break;
    }
  }
}

void PlaceBlobsCentre(PaintContext& ctx, const PlaceParams& p) {
  *ctx.blob_stamp = 1;

  const int total = UsableCellCount(ctx.grid);
  const double total_d = static_cast<double>(total);
  const double target =
      (1.0 - static_cast<double>(p.water_amount) * 0.01) * (0.5 - 0.45) + 0.45;
  const int cap = Ftol(total_d * 0.03 * target);

  // 地图正中的等距坐标。⚠️ 这两个值在整个函数里**再也不变**：
  // 原版把它们留在 ebx/ebp 里，循环里既没重算也没回写。
  const int centre_y = ctx.grid.height() / 2 + ctx.grid.width() / 2;
  const int centre_x = centre_y + 1;
  const CellCoord map_centre{centre_x, centre_y};

  ClearQueuedMarks(ctx);

  const Rect region{p.usable.x + 1, p.usable.y + 1, p.usable.w - 2,
                    p.usable.h - 2};

  if (!(0.0 < target)) return;

  CellCoord start = map_centre;
  int painted = 0;
  int iter = 0;
  double ratio = 0.0;

  for (;;) {
    if (iter >= 100) break;

    int budget = Ftol((target - ratio) * total_d);
    if (budget >= cap) budget = cap;

    PaintParams pp;
    pp.budget = budget;
    pp.region = region;
    pp.start = start;
    pp.ellipse = true;
    pp.target = map_centre;   // ⚠️ 固定是地图正中，只有起点在动
    pp.walk_sigma = 0.75;
    pp.directional = true;

    painted += PaintBlob(ctx, pp);
    ++iter;
    ratio = static_cast<double>(painted) / total_d;

    // 下一轮起点：尚未入过堆、且离**地图正中**曼哈顿距离最近的格子。
    // 比较是严格小于，所以并列时迭代器先访问到的那格赢。
    std::uint32_t best_packed = 0;
    int best_dist = 0xc350;   // 50000
    ctx.grid.ForEachCell([&](const CellCoord& c) {
      if ((*ctx.work)[ctx.grid.WorkIndex(c)].queued != 0) return;
      const int d = Abs(c.y - centre_y) + Abs(c.x - centre_x);
      if (d < best_dist) {
        best_dist = d;
        best_packed = c.Packed();
      }
    });
    // 一个都没找到时 best_packed 保持 0 -> 起点变成 (0,0)，涂块改用随机起点。
    start = CellCoord::FromPacked(best_packed);

    if (!(ratio < target)) break;
  }
}

void PlaceBlobsHalves(PaintContext& ctx, const PlaceParams& p) {
  *ctx.blob_stamp = 1;

  const int total = UsableCellCount(ctx.grid);
  const double total_d = static_cast<double>(total);
  const double target =
      (1.0 - static_cast<double>(p.water_amount) * 0.01) * (0.2 - 0.15) + 0.15;
  const int cap = Ftol(total_d * 0.06 * target);

  ClearQueuedMarks(ctx);

  // 抛硬币：< 0.5 竖着切，>= 0.5 横着切。两半都比半幅**再窄 1**（half - 1），
  // 中间那一格谁也不要。
  const double coin =
      static_cast<double>(static_cast<std::int64_t>(ctx.rng->Random())) *
      kInvUintMax;
  Rect halves[2];
  if (coin < 0.5) {
    const int half = p.usable.w / 2;
    halves[0] = Rect{p.usable.x, p.usable.y, half - 1, p.usable.h};
    halves[1] = Rect{half + p.usable.x + 1, p.usable.y, half - 1, p.usable.h};
  } else {
    const int half = p.usable.h / 2;
    halves[0] = Rect{p.usable.x, p.usable.y, p.usable.w, half - 1};
    halves[1] = Rect{p.usable.x, half + p.usable.y + 1, p.usable.w, half - 1};
  }

  const int side = ctx.grid.side();
  // ⚠️ iter 在两半之间是**共用**的：0x59B22E 在外层循环之前清零，
  // 循环体里只清 painted 和 ratio。
  int iter = 0;

  for (int k = 0; k < 2; ++k) {
    const Rect& rc = halves[k];
    const CellCoord centre = RectCentreToIso(rc, ctx.grid.width());
    CellCoord start = centre;
    int painted = 0;
    double ratio = 0.0;

    // 各向异性权重：长边那一侧的位移被放大 1.2·长/短 倍，
    // 效果是让下一个起点更愿意沿长轴走。
    double aspect_x, aspect_y;
    if (rc.h > rc.w) {
      aspect_x = (static_cast<double>(rc.h) / static_cast<double>(rc.w)) * 1.2;
      aspect_y = 1.0;
    } else {
      aspect_x = 1.0;
      aspect_y = (static_cast<double>(rc.w) / static_cast<double>(rc.h)) * 1.2;
    }
    const double hw = static_cast<double>(rc.w) * 0.5;
    const double hh = static_cast<double>(rc.h) * 0.5;
    const double ellipse_a = 1.0 / (hw * hw);
    const double ellipse_b = 1.0 / (hh * hh);

    if (!(0.0 < target)) continue;

    // 等距坐标下这半张图的包围范围（0x59B54A-0x59B585）。
    const int lo_sum = ctx.grid.width() + 2 * rc.y + 1;
    const int hi_sum = lo_sum + 2 * rc.h + 2;
    const int lo_dif = 2 * rc.x - ctx.grid.width() + 1;
    const int hi_dif = lo_dif + 2 * rc.w + 2;

    for (;;) {
      if (iter >= 100) break;

      int budget = Ftol((target - ratio) * total_d);
      if (budget >= cap) budget = cap;

      PaintParams pp;
      pp.budget = budget;
      pp.region = rc;
      pp.start = start;
      pp.ellipse = true;
      pp.target = centre;   // ⚠️ 同 MapType 1：目标固定，只有起点在动
      pp.walk_sigma = 0.75;
      pp.directional = true;

      painted += PaintBlob(ctx, pp);
      ++iter;
      ratio = static_cast<double>(painted) / total_d;

      // 下一轮起点。这里**不走全图迭代器**，而是线性扫整个工作数组
      // （side² 条，含菱形外的空记录）——顺序与 MapType 1 不同，不能互换。
      std::uint32_t best_packed = 0;
      int best_score = 0xc350;
      for (int i = 0; i < side * side; ++i) {
        // 菱形外的记录在阶段 1 没被写过坐标，读出来是 (0,0)，
        // 会被下面的包围范围判定直接刷掉。
        const int wx = i % side;
        const int wy = i / side;
        const bool real = ctx.grid.InMap(wx, wy);
        const int x = real ? wx : 0;
        const int y = real ? wy : 0;

        const int sum = x + y;
        const int dif = x - y;
        if (sum < lo_sum || sum > hi_sum) continue;
        if (dif < lo_dif || dif > hi_dif) continue;
        if ((*ctx.work)[i].queued != 0) continue;

        const CellCoord c{x, y};
        if (!InRegion(c, rc, true, ellipse_a, ellipse_b, ctx.full_bounds,
                      ctx.grid.width())) {
          continue;
        }
        const int score =
            Ftol(static_cast<double>(Abs(y - centre.y)) * aspect_y +
                 static_cast<double>(Abs(x - centre.x)) * aspect_x);
        if (score < best_score) {
          best_score = score;
          best_packed = c.Packed();
        }
      }
      start = CellCoord::FromPacked(best_packed);

      if (!(ratio < target)) break;
    }
  }
}

void PlaceBlobs(PaintContext& ctx, const PlaceParams& p, int map_type) {
  switch (map_type) {
    case 0: PlaceBlobsGrid(ctx, p); break;
    case 1: PlaceBlobsCentre(ctx, p); break;
    case 2: PlaceBlobsHalves(ctx, p); break;
    default: break;
  }
}

}  // namespace rmg
}  // namespace ra2
