#include "blob_paint.h"

#include <cmath>
#include <cstdint>

#include "blob_heap.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

// 0x7ED898：1/(2^32-1)。整个生成器只有这一个"把 uint32 摊到 [0,1]"的常数。
constexpr double kInvUintMax = 2.3283064370807974e-10;
constexpr double kJitterScale = 5.0;   // 0x7ED808
constexpr double kAspectBias = 1.2;    // 0x7E5190
constexpr double kHalf = 0.5;          // 0x7E1738

// 0x89F688 八邻域增量表，涂块按步长 2 只取 0/2/4/6。
constexpr int kAdjX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kAdjY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// _ftol：向零截断。RC=chop 下与 C 的 (int) 转换语义一致。
inline int Ftol(double v) { return static_cast<int>(v); }

// 0x59BCAC / 0x59BD01 的内联拒绝采样。
// ⚠️ 乘法**分两次**：先乘上限再乘 1/(2^32-1)，不是乘一个预先算好的比例常数。
// scaled_random.h 里那个 PickBelow 走的是另一条路（常数已折叠），两者会在
// 末位分岔，不能互相替代。
int PickBelowMul(Randomizer& rng, int limit) {
  const double scale = static_cast<double>(static_cast<std::int64_t>(limit));
  const int hi = limit - 1;
  for (;;) {
    const std::uint32_t r = rng.Random();
    const double u = static_cast<double>(static_cast<std::int64_t>(r)) * scale * kInvUintMax;
    const int v = Ftol(u);
    // 0x59BCDA cmp/ja 是**无符号**比较
    if (static_cast<unsigned>(v) <= static_cast<unsigned>(hi)) return v;
  }
}

// 0x59BD31-0x59BD75：等距变换全程用 int16 打包分量做加法，这里照抄宽度。
inline CellCoord IsoFromReal(int rx, int ry, int w) {
  const std::int16_t nx = static_cast<std::int16_t>(-rx);
  const std::int16_t ww = static_cast<std::int16_t>(w);
  const std::int16_t x0 = static_cast<std::int16_t>(rx + 1);
  const std::int16_t y0 = static_cast<std::int16_t>(ww + nx);
  const std::int16_t x = static_cast<std::int16_t>(x0 + static_cast<std::int16_t>(ry));
  const std::int16_t y = static_cast<std::int16_t>(static_cast<std::int16_t>(ry) + y0);
  return CellCoord{x, y};
}

}  // namespace

int PaintBlob(PaintContext& ctx, const PaintParams& params) {
  MapGrid& grid = ctx.grid;
  PaintWorld& world = *ctx.world;
  std::vector<PaintWorkCell>& work = *ctx.work;
  Randomizer& rng = *ctx.rng;
  const int stamp = *ctx.blob_stamp;

  // ---- 0x59BBD1：预算下限 400；节点池 max(budget*8+2, 100) ----
  int budget = params.budget;
  if (budget <= 0x190) budget = 0x190;
  int pool_cap = budget * 8 + 2;
  if (pool_cap <= 100) pool_cap = 100;

  NodePool pool(pool_cap);
  MinHeap heap(&pool, pool_cap);

  // ---- 起点：两分量都为 0 时随机取，最多 200 次 ----
  CellCoord cur = params.start;
  if (cur.x == 0 && cur.y == 0) {
    int attempts = 0;
    for (;;) {
      const int rx = PickBelowMul(rng, grid.width());
      const int ry = PickBelowMul(rng, grid.height());
      cur = IsoFromReal(rx, ry, grid.width());
      // ⚠️ 先抽再计数：第 200 次的两个随机数**照样消耗**，只是结果被丢弃
      if (++attempts >= 0xC8) return 0;
      const int wi = grid.WorkIndex(cur);
      if (work[wi].blob_id != 0) continue;
      if (!world.IsPaintable(cur)) continue;
      break;
    }
  }

  // ---- 0x59BE02：各向异性系数。区域等于全图边界时两个都留 0 ----
  float cost_a = 0.0f;   // [esp+0x2C]，DirectionalCost 的第 6 个参数
  float cost_b = 0.0f;   // [esp+0x30]，第 5 个参数
  if (!(params.region == ctx.full_bounds)) {
    const int rw = params.region.w, rh = params.region.h;
    if (rw > rh) {
      cost_b = 1.0f;
      cost_a = static_cast<float>(static_cast<double>(rw) / static_cast<double>(rh) * kAspectBias);
    } else {
      cost_a = 1.0f;
      cost_b = static_cast<float>(static_cast<double>(rh) / static_cast<double>(rw) * kAspectBias);
    }
  }
  // 0x59BF9B：椭圆判据的两个系数 1/(w/2)² 与 1/(h/2)²
  const double half_w = static_cast<double>(params.region.w) * kHalf;
  const double half_h = static_cast<double>(params.region.h) * kHalf;
  const double ellipse_a = 1.0 / (half_w * half_w);
  const double ellipse_b = 1.0 / (half_h * half_h);

  // ---- 引力中心（double，会漂移）；起点入堆 ----
  double cx = static_cast<double>(cur.x);
  double cy = static_cast<double>(cur.y);
  int painted = 0;

  const int root = pool.Alloc(cur.x, cur.y, 0.0f);
  work[grid.WorkIndex(cur)].queued = stamp;
  heap.Push(root);

  int node = heap.Pop();   // 0x59BF6B：进主循环前先弹一个

  // ---- 主循环 0x59BFE2 ----
  while (painted < budget && node >= 0) {
    cur = CellCoord{pool[node].x, pool[node].y};
    work[grid.WorkIndex(cur)].blob_id = stamp;
    world.SetIsoTile(cur, 0);            // ← 这一格由水变陆

    for (int d = 0; d < 8; d += 2) {     // 只取 0/2/4/6 四个基本方向
      const CellCoord n{static_cast<std::int16_t>(cur.x + kAdjX[d]),
                        static_cast<std::int16_t>(cur.y + kAdjY[d])};
      if (!grid.InMap(n)) continue;                       // 0x59C083 起四条不等式
      PaintWorkCell& wn = work[grid.WorkIndex(n)];
      if (wn.blob_id != 0) continue;                      // 已是陆地
      if (wn.queued == stamp) continue;                   // 已在堆里
      if (!world.IsPaintable(n)) continue;                // 0x4865D0
      if (pool.Full()) continue;                          // 池满：静默丢弃
      if (!InRegion(n, params.region, params.ellipse, ellipse_a, ellipse_b,
                    ctx.full_bounds, grid.width())) {
        continue;
      }

      // 0x59C14A：中心取整成 int16——fadd 0.5 之后 _ftol，再截成 16 位
      const CellCoord center{
          static_cast<std::int16_t>(Ftol(cx + kHalf)),
          static_cast<std::int16_t>(Ftol(cy + kHalf))};

      double pr;
      if (params.directional) {
        // 0x59C1AB：注意实参顺序是 (cost_b, cost_a)
        pr = DirectionalCost(center, n, params.region, params.target,
                             cost_b, cost_a, ctx.full_bounds);
      } else {
        const int dx = n.x - center.x;
        const int dy = n.y - center.y;
        const int d2 = dx * dx + dy * dy;
        const double dist = FastSqrt(static_cast<double>(d2));
        // ⚠️ 先 ×5 再 ×1/(2^32-1)，换序会在 1 ulp 上分岔
        const double jitter = static_cast<double>(static_cast<std::int64_t>(rng.Random())) *
                              kJitterScale * kInvUintMax;
        pr = jitter + dist;
      }

      const int nn = pool.Alloc(n.x, n.y, static_cast<float>(pr));
      wn.queued = stamp;          // 0x59C233：**先盖戳再入堆**，越界丢弃也不撤销
      heap.Push(nn);
    }

    // ---- 0x59C2BC：引力中心布朗运动，每步两个正态偏差 ----
    cx = (*ctx.normal)() * params.walk_sigma + cx;
    cy = (*ctx.normal)() * params.walk_sigma + cy;
    ++painted;
    node = heap.Pop();
    // ⚠️ 这个刚弹出来的节点在 painted == budget 时**被直接丢弃**，见下面的收尾。
  }

  // ---- 收尾 0x59C332：把堆里剩下的边缘格也铺成陆地 ----
  while (!heap.empty()) {
    node = heap.Pop();
    if (node < 0) break;
    const CellCoord c{pool[node].x, pool[node].y};
    PaintWorkCell& wc = work[grid.WorkIndex(c)];
    if (wc.blob_id == 0 && world.IsPaintable(c)) {
      world.SetIsoTile(c, 0);
      wc.blob_id = stamp;
    }
    ++painted;                 // 0x59C3EB：排空阶段也计数，会算进返回值
  }

  // ---- 0x57A0C0：铺水陆过渡砖，失败则整块回滚 ----
  const bool ok = world.SmoothShoreline(stamp);

  // 0x59C441：**无条件**扫全图，把刚铺出来的岸线砖全部撕掉。
  // 这一遍不看上面那个成功标志——本次平滑只是可行性检验，产物不留。
  const int shore = world.ShorePiecesBase();
  grid.ForEachCell([&](const CellCoord& c) {
    const int t = world.IsoTile(c);
    if (t >= shore && t < shore + 0x2A) {
      world.SetIsoTile(c, 0);
      world.SetCellSubTile(c, 0);
    }
  });

  if (!ok) {
    // 0x59C48C：整块还原成水
    const int water = world.WaterTile();
    grid.ForEachCell([&](const CellCoord& c) {
      PaintWorkCell& wc = work[grid.WorkIndex(c)];
      if (wc.blob_id != stamp) return;
      wc.blob_id = 0;
      wc.flag4b = false;
      world.SetIsoTile(c, water);
      world.SetCellSubTile(c, 0);
      world.SetCellLevel(c, ctx.cell_level);
    });
    return 0;
  }

  ++(*ctx.blob_stamp);   // 0x59C515：块编号自增，顺带作废所有 queued 戳
  return painted;
}

}  // namespace rmg
}  // namespace ra2
