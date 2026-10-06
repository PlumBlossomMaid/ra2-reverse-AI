// 阶段 10 的三段主体：播概率图 0x5A38C0、温带 0x5A3AE0、雪地 0x5A4280，
// 以及 0x59921B 那段调度。两个最小堆扩散在 lat_spread.cpp。
#include "lats.h"

#include <cstdlib>
#include <cstdint>
#include <cstdio>

#include "height_field.h"  // TruncNormal（与阶段 9 同一个骨架）
#include "place_tile.h"    // IsUndecidedTile（0x486380）
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

void LatDebug(LatContext& ctx, const char* name) {
  if (ctx.debug_hook != nullptr) ctx.debug_hook(ctx.debug_user, name, ctx);
}

// 三处 PickRange(20,40) 的**折叠**形态（0x5A3B58 / 0x5A3B88 / 0x5A3BB8）：
//   do { v = _ftol((double)(uint32)Random() * (21/(2^32-1)) + 20.0); } while (v > 40);
// ⚠️ 只有一次乘法——和外联 PickRange(0x598030) 的「两次乘法」形态不同，
// 也和 PickBelow 不同，不能互相替代。
int PickRange20To40(Randomizer& rng) {
  const double scale = 21.0 / 4294967295.0;   // [0x7EDAD0]
  for (;;) {
    const std::uint32_t r = rng.Random();
    const int v = Ftol(static_cast<double>(static_cast<std::uint64_t>(r)) * scale + 20.0);
    if (static_cast<unsigned>(v) <= 0x28u) return v;   // cmp eax,0x28 / ja
  }
}

// 逐格抛硬币（0x5A3C53 / 0x5A3D3F / 0x5A3E1B / 0x5A434D 四处同形）：
//   fild r（高 32 位为 0，无符号）; fmul 1/(2^32-1); fcomp prob; test ah,1
// C0 置位 = st < mem，即 **r*k < prob 才铺**。失败也烧随机数。
bool CoinBelow(Randomizer& rng, double prob) {
  const double u =
      static_cast<double>(static_cast<std::uint64_t>(rng.Random())) * kInvUintMax;
  return u < prob;
}

// 0x42B1F0：n = (MapRect.Height + 4) * MapRect.Width * 2
int MapScaleNumber(const LatParams& p) {
  return (p.map_rect_h + 4) * p.map_rect_w * 2;
}

struct MainTraceRange {
  bool enabled = false;
  long long begin = 0;
  long long end = 0;
};

MainTraceRange MainLatTraceRange() {
  MainTraceRange r;
  const char* s = std::getenv("RA2_RMG_LAT_MAIN_TRACE");
  if (s == nullptr || *s == '\0') return r;
  char* tail = nullptr;
  r.begin = std::strtoll(s, &tail, 10);
  r.end = r.begin;
  if (tail != nullptr && *tail == ':') {
    r.end = std::strtoll(tail + 1, nullptr, 10);
  }
  if (r.end < r.begin) {
    const long long t = r.begin;
    r.begin = r.end;
    r.end = t;
  }
  r.enabled = true;
  return r;
}

bool TraceMainCall(const Randomizer& rng, const MainTraceRange& tr) {
  if (!tr.enabled) return false;
  const long long call = rng.call_count() + 1;
  return call >= tr.begin && call <= tr.end;
}

void TraceMainCoin(const MainTraceRange& tr, Randomizer& rng,
                   const CellCoord& c, const PaintWorkCell& w,
                   LatWorld& world, const char* tag, double prob,
                   bool* hit) {
  const long long call = rng.call_count() + 1;
  const bool trace = TraceMainCall(rng, tr);
  const std::uint32_t draw = rng.Random();
  const double u = static_cast<double>(static_cast<std::uint64_t>(draw)) *
                   kInvUintMax;
  *hit = u < prob;
  if (trace) {
    std::fprintf(stderr,
                 "LATMAIN coin call=%lld cell=(%d,%d) tag=%s draw=%u "
                 "u=%.17g prob=%.17g hit=%d iso=%d ramp=%d ov=%d "
                 "e4=%d f45=%d pr=%.17g ps=%.17g pg=%.17g\n",
                 call, c.x, c.y, tag, draw, u, prob, *hit ? 1 : 0,
                 world.IsoTile(c), world.Ramp(c), world.Overlay(c),
                 world.Field0xE4(c), w.flag45 ? 1 : 0, w.prob_rough,
                 w.prob_sand, w.prob_green);
  }
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
  LatDebug(ctx, "after_seed_probabilities");
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
  LatDebug(ctx, "after_clear_lat_queued");

  // ② 三个中心尺寸，**在遍历格子之前一次性抽好**，整张地图共用
  const int pick_rough = PickRange20To40(*ctx.rng);   // 0x5A3B58
  const int pick_sand = PickRange20To40(*ctx.rng);    // 0x5A3B88
  const int pick_green = PickRange20To40(*ctx.rng);   // 0x5A3BB8
  LatDebug(ctx, "after_lat_pick_sizes");

  // ③ 逐格铺 LAT。三次机会依次是碎石 / 沙地 / 草地，任意一次命中就跳出本格
  const MainTraceRange main_trace = MainLatTraceRange();
  // ⚠️ 2026-10-04 探针：统计 main_blocks 过五条件格数与 WriteLatBlock 调用
  long long cli_main_pass = 0;
  long long cli_main_wlb = 0;
  long long cli_fail_iso = 0, cli_fail_ramp = 0, cli_fail_ov = 0;
  long long cli_fail_e4 = 0, cli_fail_f45 = 0;
  const char* cli_lat_stat = std::getenv("RA2_RMG_LAT_STAT");
  const char* cli_obj_dump = std::getenv("RA2_RMG_OBJ_DUMP");
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    PaintWorkCell& w = work[static_cast<std::size_t>(c.y * side + c.x)];
    if (!IsUndecidedTile(world.IsoTile(c))) {
      if (cli_lat_stat) ++cli_fail_iso;
      return;
    }
    if (world.Ramp(c) != 0) {
      if (cli_lat_stat) ++cli_fail_ramp;
      return;
    }
    if (world.Overlay(c) != -1) {
      if (cli_lat_stat) ++cli_fail_ov;
      return;
    }
    if (world.Field0xE4(c) != 0) {
      if (cli_lat_stat) ++cli_fail_e4;
      if (cli_obj_dump)
        std::fprintf(stderr, "OBJDUMP %d %d %d\n", c.x, c.y,
                     world.Field0xE4(c));
      return;
    }
    if (w.flag45) {
      if (cli_lat_stat) ++cli_fail_f45;
      return;
    }
    if (cli_lat_stat) ++cli_main_pass;

    // ⚠️ 这里是写死的 512，不是工作数组用的 side
    const int field_id = c.y * 0x200 + c.x;

    int mu = 0;
    int tile_base = 0;
    bool hit = false;
    TraceMainCoin(main_trace, *ctx.rng, c, w, world, "rough", w.prob_rough,
                  &hit);
    if (hit) {                                      // 0x5A3C53
      mu = pick_rough;
      tile_base = th.rough_tile;                     // [0xABC2B8]
    } else {
      TraceMainCoin(main_trace, *ctx.rng, c, w, world, "sand", w.prob_sand,
                    &hit);
      if (hit) {                                    // 0x5A3D3F
        mu = pick_sand;
        tile_base = th.sand_tile;                    // [0xABB104]
      } else {
        TraceMainCoin(main_trace, *ctx.rng, c, w, world, "green",
                      w.prob_green, &hit);
        if (hit) {                                  // 0x5A3E1B
          mu = pick_green;
          tile_base = th.green_tile;                 // [0xAA0E18]
        } else {
          return;
        }
      }
    }
    // TruncNormal(mu = pick, sigma = 20, lo = 4, hi = 80)。0x5A3C8E 起内联：
    // 回退条件 = (mu-sigma > [0x7edac8] || mu+sigma < [0x7e3cc8])——分布整体
    // 落在 [4,80] 外才重铺，回退代码保留（mu 是运行时变量，编译器证不了永假）
    // 但 mu ∈ [20,40]、sigma = 20 时实际不可达；折叠出的回退值 38.0/42.0 =
    // (80-4)*0.5 与 +4。
    const double v = TruncNormal(*ctx.normal, static_cast<double>(mu), 20.0, 4.0, 80.0);
    if (cli_lat_stat) ++cli_main_wlb;
    WriteLatBlock(ctx, c, tile_base, Ftol(v), field_id, false);
  });
  LatDebug(ctx, "after_lat_main_blocks");
  if (cli_lat_stat) {
    std::fprintf(stderr,
                 "CLILATSTAT iso_fail=%lld ramp_fail=%lld ov_fail=%lld "
                 "e4_fail=%lld f45_fail=%lld pass=%lld wlb=%lld\n",
                 cli_fail_iso, cli_fail_ramp, cli_fail_ov, cli_fail_e4,
                 cli_fail_f45, cli_main_pass, cli_main_wlb);
  }

  // ④ 全图归一，把涂出来的块换成正确的过渡片。无随机。
  ctx.grid.ForEachCell([&](const CellCoord& c) { world.SetupLAT(c); });
  LatDebug(ctx, "after_lat_setup");
  if (std::getenv("RA2_RMG_ROCKS_TRACE")) {
    std::fprintf(stderr, "TREES entry rng=%lld nd_cached=%d nd_val=%.17g\n",
                 ctx.rng->call_count(),
                 ctx.normal->has_cached() ? 1 : 0,
                 ctx.normal->has_cached() ? ctx.normal->cached() : 0.0);
    const char* occ_path = std::getenv("RA2_RMG_OCC_DUMP");
    if (occ_path) {
      FILE* f = std::fopen(occ_path, "w");
      if (f) {
        ctx.grid.ForEachCell([&](const CellCoord& c) {
          if (world.Field0xE4(c) != 0) std::fprintf(f, "%d %d\n", c.x, c.y);
        });
        std::fclose(f);
      }
    }
  }

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
    // 两处 TruncNormal 的回退分支都被编译器整个删了（不可达），这里沿用通用式。
    // 0x5A400B/0x5A4043：无回退代码（mu±sigma 完全落在 [lo,hi] 内，回退条件
    // 被折叠成永假），只有拒绝采样循环。
    const double prob = TruncNormal(*ctx.normal, 0.2, 0.1, 0.05, 0.4);
    const double size = TruncNormal(*ctx.normal, 25.0, 10.0, 10.0, 35.0);
    const int planted = GrowTreePatch(ctx, at, Ftol(size), prob);
    budget -= planted;
    // ⚠️ 2026-10-04 探针：逐轮 trace（RA2_RMG_TREES_TRACE 环境变量开启）
    if (std::getenv("RA2_RMG_TREES_TRACE")) {
      std::fprintf(stderr,
                   "TREESTR round=%d rng=%lld seed=(%d,%d) tries=%d "
                   "prob=%.17g size=%.17g planted=%d budget=%d\n",
                   round, ctx.rng->call_count(), at.x, at.y, tries,
                   prob, size, planted, budget);
    }
  }
  LatDebug(ctx, "after_lat_trees");

  // ⑥ 撒石头 / overlay
  const int n = MapScaleNumber(ctx.params);
  const bool rocks_trace = std::getenv("RA2_RMG_ROCKS_TRACE") != nullptr;
  if (rocks_trace) {
    std::fprintf(stderr, "ROCKS entry rng=%lld i1=%d i2=%d n=%d\n",
                 ctx.rng->call_count(), ctx.rng->index1(), ctx.rng->index2(), n);
  }
  // 0x5A40F0：**两次乘法**那一路，跨度 n/200+1，拒绝 > n/200（有符号除法）
  const int count = PickBelowMul(*ctx.rng, n / 200 + 1);
  if (rocks_trace) {
    std::fprintf(stderr, "ROCKS count=%d (rng=%lld)\n", count,
                 ctx.rng->call_count());
  }
  int placed = 0;
  int attempt = 0;
  if (count * 5 <= 0) {
    LatDebug(ctx, "after_lat_rocks");
    return;
  }
  do {
    if (count <= placed) {
      LatDebug(ctx, "after_lat_rocks");
      return;
    }
    int idx;
    int redraws = 0;
    do {
      // 0x5A4157：同样是两次乘法的内联形态，跨度 side²
      idx = PickBelowMul(*ctx.rng, ctx.side * ctx.side);
      ++redraws;
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
      if (rocks_trace) {
        std::fprintf(stderr, "ROCKS #%d attempt=%d idx=%d coord=(%d,%d) ov=%d "
                             "redraws=%d rng=%lld\n",
                     placed, attempt, idx, c.x, c.y, world.Overlay(c),
                     redraws, ctx.rng->call_count());
      }
    }
    ++attempt;
  } while (attempt < count * 5);
  LatDebug(ctx, "after_lat_rocks");
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
    // mu 是**写死的 20.0**，不像温带那样先 PickRange(20,40) 抽一个。
    // 0x5A4384 起内联：无回退分支（mu±sigma=[5,35]⊂[4,60]，回退条件被
    // 编译器折叠成永假），只有拒绝采样循环。
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
    if (budget < 1 || round > 99) {
      return;
    }
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
    const int planted = GrowTreePatch(ctx, at, Ftol(size), prob);
    budget -= planted;
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
