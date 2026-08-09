// 阶段 10 的三段主体：播概率图 0x5A38C0、温带 0x5A3AE0、雪地 0x5A4280，
// 以及 0x59921B 那段调度。两个最小堆扩散在 lat_spread.cpp。
#include "lats.h"

#include <cstdint>

#include "height_field.h"  // TruncNormal（与阶段 9 同一个骨架）
#include "place_tile.h"    // IsUndecidedTile（0x486380）
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

// 三处 PickRange(20,40) 的**折叠**形态（0x5A3B58 / 0x5A3B88 / 0x5A3BB8）：
//   do { v = _ftol((double)(uint32)Random() * (21/(2^32-1)) + 20.0); } while (v > 40);
// ⚠️ 只有一次乘法——和外联 PickRange(0x598030) 的「两次乘法」形态不同，
// 也和 PickBelow 不同，不能互相替代。
int PickRange20To40(Randomizer& rng) {
  const double scale = 21.0 / 4294967295.0;   // [0x7EDAD0]
  for (;;) {
    const std::uint32_t r = rng.Random();
    const int v = Ftol(static_cast<double>(static_cast<std::int64_t>(r)) * scale + 20.0);
    if (static_cast<unsigned>(v) <= 0x28u) return v;   // cmp eax,0x28 / ja
  }
}

// 逐格抛硬币（0x5A3C53 / 0x5A3D3F / 0x5A3E1B / 0x5A434D 四处同形）：
//   fild r（高 32 位为 0，无符号）; fmul 1/(2^32-1); fcomp prob; test ah,1
// C0 置位 = st < mem，即 **r*k < prob 才铺**。失败也烧随机数。
bool CoinBelow(Randomizer& rng, double prob) {
  const double u =
      static_cast<double>(static_cast<std::int64_t>(rng.Random())) * kInvUintMax;
  return u < prob;
}

// 0x42B1F0：n = (MapRect.Height + 4) * MapRect.Width * 2
int MapScaleNumber(const LatParams& p) {
  return (p.map_rect_h + 4) * p.map_rect_w * 2;
}

}  // namespace

void SeedVegetationProbabilities(LatContext& ctx) {
  LatWorld& world = *ctx.world;
  std::vector<PaintWorkCell>& work = *ctx.work;
  const int side = ctx.side;
  // 0x5A38D8：veg 只算一次，循环外
  const double veg = static_cast<double>(ctx.params.vegetation) * 0.01;

  ctx.grid.ForEachCell([&](const CellCoord& c) {
    PaintWorkCell& w = work[static_cast<std::size_t>(c.y * side + c.x)];
    if (!world.IsShorePiece(c)) {
      // 只有「还没被别的阶段占过」才播种
      if (w.prob_sand == 0.0) {
        w.prob_sand = 0.005;              // [0x7ED7D8]
        w.prob_rough = 0.02 * veg;        // [0x7ED7E0]
        w.prob_green = 0.005;             // [0x7ED7E8]
      }
      return;
    }
    // ⚠️ 原版 bug（0x5A3909-0x5A3920）：写目标指针在进入 5×5 循环**之前**
    // 就按被迭代格自己的 XY 算好了，循环变量只参与菱形判定、不参与寻址。
    // 于是本该「岸边一圈放大」，实际是**对中心格重复写最多 25 次同一个值**。
    // 照抄：判定用邻居坐标，写入用中心格。
    for (int x2 = c.x - 2; x2 < c.x + 3; ++x2) {
      for (int y2 = c.y - 2; y2 < c.y + 3; ++y2) {
        if (!ctx.grid.InMap(static_cast<std::int16_t>(x2),
                            static_cast<std::int16_t>(y2))) {
          continue;
        }
        w.prob_sand = 0.005;
        const double rough = 0.02 * veg;
        w.prob_green = 0.0;
        w.prob_rough = rough * 10.0;      // [0x7E44A8]
      }
    }
  });
}

void CreateLatsTemperate(LatContext& ctx) {
  LatWorld& world = *ctx.world;
  std::vector<PaintWorkCell>& work = *ctx.work;
  const int side = ctx.side;
  const TheaterData& th = *ctx.theater;

  // ① 清全图工作记录的 +0x3C（田块 id），给下面的 LAT 块写入器复用
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    work[static_cast<std::size_t>(c.y * side + c.x)].queued = 0;
  });

  // ② 三个中心尺寸，**在遍历格子之前一次性抽好**，整张地图共用
  const int pick_rough = PickRange20To40(*ctx.rng);   // 0x5A3B58
  const int pick_sand = PickRange20To40(*ctx.rng);    // 0x5A3B88
  const int pick_green = PickRange20To40(*ctx.rng);   // 0x5A3BB8

  // ③ 逐格铺 LAT。三次机会依次是碎石 / 沙地 / 草地，任意一次命中就跳出本格
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    PaintWorkCell& w = work[static_cast<std::size_t>(c.y * side + c.x)];
    if (!IsUndecidedTile(world.IsoTile(c))) return;
    if (world.Ramp(c) != 0) return;
    if (world.Overlay(c) != -1) return;
    if (world.Field0xE4(c) != 0) return;
    if (w.flag45) return;

    // ⚠️ 这里是写死的 512，不是工作数组用的 side
    const int field_id = c.y * 0x200 + c.x;

    int mu = 0;
    int tile_base = 0;
    if (CoinBelow(*ctx.rng, w.prob_rough)) {         // 0x5A3C53
      mu = pick_rough;
      tile_base = th.rough_tile;                     // [0xABC2B8]
    } else if (CoinBelow(*ctx.rng, w.prob_sand)) {   // 0x5A3D3F
      mu = pick_sand;
      tile_base = th.sand_tile;                      // [0xABB104]
    } else if (CoinBelow(*ctx.rng, w.prob_green)) {  // 0x5A3E1B
      mu = pick_green;
      tile_base = th.green_tile;                     // [0xAA0E18]
    } else {
      return;
    }
    // TruncNormal(mu = pick, sigma = 20, lo = 4, hi = 80)。回退分支的
    // sigma/mu 被折叠成 38.0/42.0 = (80-4)*0.5 与 +4，与通用式一致；
    // 而 mu ∈ [20,40]、sigma = 20 使它**永远不可达**，保留即可。
    const double v = TruncNormal(*ctx.normal, static_cast<double>(mu), 20.0, 4.0, 80.0);
    WriteLatBlock(ctx, c, tile_base, Ftol(v), field_id, false);
  });

  // ④ 全图归一，把涂出来的块换成正确的过渡片。无随机。
  ctx.grid.ForEachCell([&](const CellCoord& c) { world.SetupLAT(c); });

  // ⑤ 树林补丁：预算 + 最多 100 轮
  // budget = _ftol((Width*0.1 + 0.7) * (Vegetation*0.01) * MaxTrees)
  // 0x5A3F5B 是 fimul（整数乘），所以 MaxTrees 走的是整型→双精度那条路
  int budget = Ftol((static_cast<double>(ctx.params.width) * 0.1 + 0.7) *
                    (static_cast<double>(ctx.params.vegetation) * 0.01) *
                    static_cast<double>(ctx.params.max_trees));
  for (int round = 0; budget > 0 && round < 100; ++round) {
    // 挑一格。⚠️ 温带这边走的是**外联**的 PickRange(0x598030)
    int tries = 0;
    CellCoord at;
    for (;;) {
      int idx;
      do {
        idx = PickRange(*ctx.rng, 0, ctx.side * ctx.side - 1);   // 0x59805E
        // 菱形外的槽位坐标恒为 (0,0)，被拒绝时 tries 不加，随机数照花
      } while (work[static_cast<std::size_t>(idx)].coord == CellCoord{0, 0});
      ++tries;
      at = work[static_cast<std::size_t>(idx)].coord;
      if (tries > 200) {
        // 0x5A3FCC：不是放弃，而是**强行用格 (0,0)**
        at = CellCoord{0, 0};
        break;
      }
      if (IsUndecidedTile(world.IsoTile(at))) break;
    }
    // 两处 TruncNormal 的回退分支都被编译器整个删了（不可达），这里沿用通用式
    const double prob = TruncNormal(*ctx.normal, 0.2, 0.1, 0.05, 0.4);
    const double size = TruncNormal(*ctx.normal, 25.0, 10.0, 10.0, 35.0);
    budget -= GrowTreePatch(ctx, at, Ftol(size), prob);
  }

  // ⑥ 撒石头 / overlay
  const int n = MapScaleNumber(ctx.params);
  // 0x5A40F0：**两次乘法**那一路，跨度 n/200+1，拒绝 > n/200（有符号除法）
  const int count = PickBelowMul(*ctx.rng, n / 200 + 1);
  int placed = 0;
  int attempt = 0;
  if (count * 5 <= 0) return;
  do {
    if (count <= placed) return;
    int idx;
    do {
      // 0x5A4157：同样是两次乘法的内联形态，跨度 side²
      idx = PickBelowMul(*ctx.rng, ctx.side * ctx.side);
    } while (work[static_cast<std::size_t>(idx)].coord == CellCoord{0, 0});
    const CellCoord c = work[static_cast<std::size_t>(idx)].coord;
    if (world.Overlay(c) == -1) {
      if (world.IsClearToSandLat(c)) {                     // 0x486790
        world.SetOverlay(c, PickBelow(*ctx.rng, 5) + 0xA8);   // 0x5A41DE
      } else if (IsUndecidedTile(world.IsoTile(c)) || world.IsGreen(c)) {
        world.SetOverlay(c, PickBelow(*ctx.rng, 5) + 0xAD);   // 0x5A4221
      }
      // ⚠️ 三类都不沾边的格子（+0x44 == -1 但既非沙 LAT 也非平地/绿地）
      // 会跳过赋值，却**照样**走下面三行——白白吃掉一个石头名额
      world.SetOverlayData(c, 0);
      world.RecalcAttributes(c, -1);
      ++placed;
    }
    ++attempt;
  } while (attempt < count * 5);
}

void CreateLatsSnow(LatContext& ctx) {
  LatWorld& world = *ctx.world;
  std::vector<PaintWorkCell>& work = *ctx.work;
  const int side = ctx.side;
  const TheaterData& th = *ctx.theater;

  // ① 清全图 +0x3C
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    work[static_cast<std::size_t>(c.y * side + c.x)].queued = 0;
  });

  // ② 逐格一次抛硬币。门槛比温带少了 +0x44 == -1 与 +0xE4 == 0 两条
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    PaintWorkCell& w = work[static_cast<std::size_t>(c.y * side + c.x)];
    if (!IsUndecidedTile(world.IsoTile(c))) return;
    if (world.Ramp(c) != 0) return;
    if (w.flag45) return;

    const int field_id = c.y * 0x200 + c.x;
    if (!CoinBelow(*ctx.rng, w.prob_sand)) return;        // 0x5A434D
    // mu 是**写死的 20.0**，不像温带那样先 PickRange(20,40) 抽一个
    const double v = TruncNormal(*ctx.normal, 20.0, 15.0, 4.0, 60.0);
    WriteLatBlock(ctx, c, th.rough_tile, Ftol(v), field_id, false);
  });

  // ③ 全图归一
  ctx.grid.ForEachCell([&](const CellCoord& c) { world.SetupLAT(c); });

  // ④ 树林补丁。与温带 §⑤ 逐字节同构，只有挑格那处是**内联**的两次乘法
  int budget = Ftol((static_cast<double>(ctx.params.width) * 0.1 + 0.7) *
                    (static_cast<double>(ctx.params.vegetation) * 0.01) *
                    static_cast<double>(ctx.params.max_trees));
  int round = 0;
  for (;;) {
    if (budget < 1 || round > 99) return;
    int tries = 0;
    CellCoord at;
    for (;;) {
      int idx;
      do {
        idx = PickBelowMul(*ctx.rng, ctx.side * ctx.side);   // 0x5A447D
      } while (work[static_cast<std::size_t>(idx)].coord == CellCoord{0, 0});
      ++tries;
      at = work[static_cast<std::size_t>(idx)].coord;
      if (tries > 200) {
        at = CellCoord{0, 0};
        break;
      }
      if (IsUndecidedTile(world.IsoTile(at))) break;
    }
    const double prob = TruncNormal(*ctx.normal, 0.2, 0.1, 0.05, 0.4);
    const double size = TruncNormal(*ctx.normal, 25.0, 10.0, 10.0, 35.0);
    budget -= GrowTreePatch(ctx, at, Ftol(size), prob);
    ++round;
  }
}

void CreateLats(LatContext& ctx) {
  std::vector<PaintWorkCell>& work = *ctx.work;
  const int side = ctx.side;
  if (ctx.params.theater == 0) {
    SeedVegetationProbabilities(ctx);
    CreateLatsTemperate(ctx);
    return;
  }
  // 雪地：调度处内联铺两个常量（[0x7ED7F0] = 0.005、[0x7ED7F8] = 0.001），
  // **不写 +0x18**——它在雪地路径上是上一阶段的残值，但 0x5A4280 不读它
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    PaintWorkCell& w = work[static_cast<std::size_t>(c.y * side + c.x)];
    w.prob_sand = 0.005;
    w.prob_green = 0.001;
  });
  CreateLatsSnow(ctx);
}

}  // namespace rmg
}  // namespace ra2
