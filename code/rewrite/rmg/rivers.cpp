#include "rivers.h"

#include <cstdint>
#include <cstdio>

#include "blob_region.h"
#include "fast_math.h"
#include "place_tile.h"
#include "scaled_random.h"
#include "water_blob.h"
#include "waterfall.h"

namespace ra2 {
namespace rmg {

namespace {

inline bool InMap(const MapGrid& g, const CellCoord& c) {
  return g.InMap(c.x, c.y);
}

inline std::size_t Idx(const RegionContext& ctx, const CellCoord& c) {
  const long long i =
      static_cast<long long>(c.y) * ctx.grid.side() + c.x;
  if (i < 0) return 0;
  const long long n = static_cast<long long>(ctx.work->size());
  return static_cast<std::size_t>(i >= n ? n - 1 : i);
}
inline PaintWorkCell& Rec(RegionContext& ctx, const CellCoord& c) {
  return (*ctx.work)[Idx(ctx, c)];
}

// 通过确定路径访问 RegionWorld 接口（消菱形二义）
inline void SetSub(RiverWorld& w, const CellCoord& c, int v) {
  static_cast<WaterBlobWorld&>(w).SetSubTile(c, v);
}
inline void SetLvl(RiverWorld& w, const CellCoord& c, int v) {
  static_cast<WaterBlobWorld&>(w).SetLevel(c, v);
}
inline int GetLvl(const RiverWorld& w, const CellCoord& c) {
  return static_cast<const WaterBlobWorld&>(w).Level(c);
}
inline void TouchW(RiverWorld& w, const CellCoord& c) {
  if (!static_cast<WaterBlobWorld&>(w).CellExists(c))
    static_cast<WaterBlobWorld&>(w).SetDummyCoord(c);
}

}  // namespace

bool River(RegionContext& ctx, RiverWorld& world, RiverParams& p,
           const CellCoord& start, double angle, bool is_branch) {
  long long rng_entry = ctx.rng->call_count();
  std::printf("DEBUG: River() entry, rng=%lld\n", rng_entry);
  std::fflush(stdout);

  // 详细RNG trace（仅第一个case）
  static bool first_trace = true;
  bool do_trace = first_trace && start.x == 0 && start.y == 0 && !is_branch;
  if (do_trace) first_trace = false;

  Randomizer& rng   = *ctx.rng;
  NormalDeviate& nd = *ctx.normal;
  const MapGrid& g  = ctx.grid;
  const int W = ctx.map_w;
  const int H = ctx.map_h;

  CellCoord cur = start;
  double ang = angle;

  if (cur.x == 0 && cur.y == 0) {
    long long before_pick = rng.call_count();
    uint32_t r1 = rng.Random();
    uint32_t r2 = rng.Random();
    uint32_t r3 = rng.Random();
    std::printf("DEBUG: RNG#1-3: 0x%08X 0x%08X 0x%08X\n", r1, r2, r3);
    int dir = r1 % 4;
    int rx  = r2 % W;
    int ry  = r3 % H;
    std::printf("DEBUG: PickBelow x3 consumed=%lld RNG\n",
                rng.call_count() - before_pick);
    std::fflush(stdout);

    constexpr double kPi4 = 0.7853981633974483;
    constexpr double kPi6 = 0.5235987755982988;
    double M  = 7.0 * kPi4 - static_cast<double>(dir) * 2.0 * kPi4;
    double HI = M + kPi4;
    double LO = M - kPi4;
    double sigma = kPi6, mean = M;
    // 死分支：恒不成立，照抄
    if (HI < M - kPi6 || M + kPi6 < LO) {
      sigma = (HI - LO) * 0.5;
      mean  = sigma + LO;
    }
    long long rng_before = rng.call_count();
    double ang_sample;
    do {
      ang_sample = nd();
      ang = ang_sample * sigma + mean;
      std::printf("DEBUG: angle nd()=%.17g ang=%.17g [%.17g, %.17g]\n",
                  ang_sample, ang, LO, HI);
    } while (ang < LO || ang > HI);

    std::printf("DEBUG: angle_sampling consumed=%lld RNG calls\n",
                rng.call_count() - rng_before);
    std::fflush(stdout);

    // ISO 入口点（四边入口，见 docs §四的表）
    const auto s16 = [](int v) {
      return static_cast<std::int16_t>(v);
    };
    std::int16_t cx, cy;
    switch (dir) {
      case 0: cx = s16(rx + 1);           cy = s16(W - rx);      break;
      case 1: cx = s16(W + H - 1 - ry);  cy = s16(H - ry);      break;
      case 2: cx = s16(W + H - 1 - rx);  cy = s16(H + rx);      break;
      default: cx = s16(ry + 1);          cy = s16(ry + W);      break;
    }
    cur = CellCoord{cx, cy};
  }

  if (!InMap(g, cur)) return false;

  const int initialLevel = p.cell_level;

  // 河宽与跌水阈值
  double wf = static_cast<double>(p.water_amount) * 0.07;
  if (wf < 1.0) wf = 1.0;
  int wmax  = Ftol(wf);
  long long before_width = rng.call_count();
  int width = PickRange(rng, 1, wmax);
  std::printf("DEBUG: PickRange(width) consumed=%lld RNG\n",
              rng.call_count() - before_width);
  std::fflush(stdout);
  int half  = width / 2;
  int widthLo = width - half;
  int widthHi = width + half;
  double curWidth = static_cast<double>(width);
  long long before_fall = rng.call_count();
  int fallThreshold = PickRange(rng, 35, 125);
  std::printf("DEBUG: PickRange(fallThreshold) consumed=%lld RNG\n",
              rng.call_count() - before_fall);
  std::fflush(stdout);

  double x = static_cast<double>(cur.x);
  double y = static_cast<double>(cur.y);
  const double ang0 = ang;

  bool ok        = true;
  int  steps     = 0;
  int  fallCount = 0;
  double u;

  CellCoord crossA{}, crossB{};

  do {
    double c = Cos(ang);
    double s = Sin(ang);
    int    n = Ftol(curWidth);
    if (steps <= 3) {
      std::printf("DEBUG: step=%d c=%.3f s=%.3f n=%d\n", steps, c, s, n);
      std::fflush(stdout);
    }

    double xL = x - static_cast<double>(n - 1) * s * 0.5;
    double yL = y - static_cast<double>(n - 1) * c * 0.5;

    bool flagA = true, flagB = true;

    for (int i = 0; i < n; ++i) {
      int cx = Ftol(xL + static_cast<double>(i) * s);
      int cy = Ftol(yL + static_cast<double>(i) * c);
      CellCoord cc{static_cast<std::int16_t>(cx),
                   static_cast<std::int16_t>(cy)};
      if (!InMap(g, cc)) continue;
      TouchW(world, cc);
      PaintWorkCell& rec = Rec(ctx, cc);
      int bid = rec.blob_id;
      if (bid == 0 && IsUndecidedTile(world.Tile(cc))) {
        rec.blob_id = p.blob_id;
        world.SetTile(cc, ctx.theater->water_set);
        SetSub(world, cc, 0);
        SetLvl(world, cc, p.cell_level);
        if (steps <= 2 || (cc.x == 11 && cc.y == 27)) {
          std::printf("DEBUG: step=%d painted (%d,%d) level=%d\n",
                      steps, cc.x, cc.y, p.cell_level);
          std::fflush(stdout);
        }
      } else if (bid != 0 && bid != p.blob_id) {
        ok = false;
        if (steps <= 10) {
          std::printf("DEBUG: step=%d COLLISION at (%d,%d) bid=%d != mine=%d\n",
                      steps, cc.x, cc.y, bid, p.blob_id);
          std::fflush(stdout);
        }
      }
    }
    crossA = CellCoord{static_cast<std::int16_t>(Ftol(xL)),
                       static_cast<std::int16_t>(Ftol(yL))};
    crossB = CellCoord{
        static_cast<std::int16_t>(Ftol(xL + static_cast<double>(n-1)*s)),
        static_cast<std::int16_t>(Ftol(yL + static_cast<double>(n-1)*c))};

    double ac = c < 0.0 ? -c : c;
    double as = s < 0.0 ? -s : s;
    if (ac <= as) flagA = false; else flagB = false;

    x += c;
    y -= s;

    // 瀑布
    if (!is_branch && (flagA || flagB) && fallCount < 1) {
      int dang  = Ftol(ang - ang0);
      int adang = dang < 0 ? -dang : dang;
      if (static_cast<double>(adang) < 0.7853981633974483 &&
          p.allow_waterfall && steps > fallThreshold) {
        int wdir;
        if (!flagA)  wdir = (s <= 0.0) ? 4 : 0;
        else         wdir = (c <= 0.0) ? 6 : 2;
        WaterfallParams wp{p.blob_id, p.blob_id};
        bool wok = false;
        PlaceWaterfall(ctx, world, wp, crossA, crossB,
                       wdir, &wok, &x, &y);
        if (wok) { ++fallCount; p.blob_id += 1; }
      }
    }

    // 支流（约 1% 概率）
    {
      uint32_t ru = static_cast<uint32_t>(rng.Random());
      double ub   = static_cast<double>(ru) * kInvUintMax;
      if (steps <= 10) {
        std::printf("DEBUG: step=%d branch_check ru=%u ub=%.6f trigger=%d\n",
                    steps, ru, ub, (ub < 0.01 && ok && !is_branch && fallCount == 0) ? 1 : 0);
        std::fflush(stdout);
      }
      if (ub < 0.01 && ok && !is_branch && fallCount == 0) {
        constexpr double kPi6  = 0.5235987755982988;
        constexpr double k5Pi6 = 2.6179938779914944;
        double bM  = ang + 1.5707963267948966;
        double bHI = ang + k5Pi6;
        double bLO = ang + kPi6;
        double bSig = kPi6, bMean = bM;
        if (bHI < bM - kPi6 || bM + kPi6 < bLO) {
          bSig  = (bHI - bLO) * 0.5;
          bMean = bSig + bLO;
        }
        double bang;
        do { bang = nd() * bSig + bMean; } while (bang < bLO || bang > bHI);
        CellCoord bs{static_cast<std::int16_t>(Ftol(x)),
                     static_cast<std::int16_t>(Ftol(y))};
        River(ctx, world, p, bs, bang, true);
      }
    }

    // 角度游走（步数 > 5 后开始）
    if (steps > 5) {
      long long before_ang = rng.call_count();
      bool cached_before = nd.has_cached();
      constexpr double kPi10 = 0.3141592653589793;
      double dLO = -kPi10, dHI = kPi10;
      double dSig = kPi10, dMean = 0.0;
      if (dHI < dMean - kPi10 || dMean + kPi10 < dLO) {
        dSig  = (dHI - dLO) * 0.5;
        dMean = dSig + dLO;
      }
      double da;
      int attempts = 0;
      do { da = nd() * dSig + dMean; ++attempts; } while (da < dLO || da > dHI);
      ang += da;
      if (steps <= 10) {
        std::printf("DEBUG: step=%d angle_walk cache_before=%d attempts=%d consumed=%lld RNG\n",
                    steps, cached_before?1:0, attempts, rng.call_count() - before_ang);
        std::fflush(stdout);
      }
    }

    // 宽度游走（half > 0 才调整）
    if (half > 0) {
      long long before_width = rng.call_count();
      bool cached_before = nd.has_cached();
      double wLO = static_cast<double>(widthLo) - curWidth;
      double wHI = static_cast<double>(widthHi) - curWidth;
      double wSig = 0.5, wMean = 0.0;
      if (wHI < wMean - 0.5 || wMean + 0.5 < wLO) {
        wSig  = (wHI - wLO) * 0.5;
        wMean = wSig + wLO;
      }
      long long before_loop = rng.call_count();
      double dw;
      int attempts = 0;
      do {
        dw = nd() * wSig + wMean;
        ++attempts;
      } while (dw < wLO || dw > wHI);
      curWidth += dw;
      if (steps <= 10) {
        std::printf("DEBUG: step=%d width_walk cache_before=%d attempts=%d loop_consumed=%lld total_consumed=%lld RNG\n",
                    steps, cached_before?1:0, attempts,
                    rng.call_count() - before_loop, rng.call_count() - before_width);
        std::fflush(stdout);
      }
    }

    ++steps;
    uint32_t ru2 = static_cast<uint32_t>(rng.Random());
    u = static_cast<double>(ru2) * kInvUintMax;
    if (do_trace || steps <= 10) {
      std::printf("DEBUG: step=%d ru2=%u u=%.17g exit=%d\n", steps, ru2, u, (u < 0.005) ? 1 : 0);
      std::fflush(stdout);
    }
  } while (u >= 0.005);

  std::printf("DEBUG: total_steps=%d\n", steps);
  std::fflush(stdout);

  // 步数不足 40 → 失败
  if (steps < 40) ok = false;

  // 河口挖湖
  if (u < 0.005 && ok) {
    int mx = Ftol(x);
    int my = Ftol(y);
    CellCoord mouth{static_cast<std::int16_t>(mx),
                    static_cast<std::int16_t>(my)};
    if (InMap(g, mouth)) {
      WaterBlobParams bp{p.water_amount, p.painted_total,
                         p.blob_id, p.cell_level};
      bool lake_ok = WaterBlob(ctx, world, bp, mouth);
      p.painted_total = bp.painted_total;
      p.blob_id       = bp.blob_id;
      if (!lake_ok) ok = false;
    }
  }

  // 成功收尾（非支流）
  if (!is_branch && ok) {
    ok = static_cast<WaterBlobWorld&>(world).SmoothShoreline(p.blob_id);
    if (ok) {
      ok = Dilate(ctx, p.blob_id, 1, 0, 0, 0x200, 0x200, false, 0);
      if (ok) {
        int bid = p.blob_id;
        ctx.grid.ForEachCell([&](const CellCoord& cc) {
          TouchW(world, cc);
          int t = world.Tile(cc);
          if (Rec(ctx, cc).blob_id == bid &&
              (t == 0 || t == static_cast<int>(0xFFFF))) {
            world.SetTile(cc, ctx.theater->green_tile);
          }
        });
      }
    }
  }

  // 峡谷 / 坡道（无条件执行，除非满足峡谷条件）
  if (fallCount == 0 && ok && p.cell_level == 4) {
    if (!is_branch) {
      uint32_t ru3 = static_cast<uint32_t>(rng.Random());
      u = static_cast<double>(ru3) * kInvUintMax;
    }
    if (u < 0.7) {
      Rect full{0, 0, 0x200, 0x200};
      if (AngularGrow(ctx, p.blob_id, 0.01f, full, cur, true)) {
        if (Dilate(ctx, p.blob_id, 6, 0, 0, 0x200, 0x200, false, 0)) {
          int bid = p.blob_id;
          ctx.grid.ForEachCell([&](const CellCoord& cc) {
            TouchW(world, cc);
            if (Rec(ctx, cc).blob_id != bid) {
              SetLvl(world, cc, GetLvl(world, cc) + 4);
            }
          });
          p.cell_level += 4;
        }
      }
    } else {
      Dilate(ctx, p.blob_id, 2, 0, 0, 0x200, 0x200, true, p.cell_level);
    }
  } else {
    bool dilate_ok = Dilate(ctx, p.blob_id, 2, 0, 0, 0x200, 0x200, true, p.cell_level);
    std::printf("DEBUG: final Dilate(rings=2, flag=true, level=%d) returned %s\n",
                p.cell_level, dilate_ok ? "true" : "false");
    // 检查问题细胞的状态
    if (ctx.grid.InMap(11, 27)) {
      auto& rec = Rec(ctx, CellCoord{11, 27});
      std::printf("DEBUG: after Dilate, cell(11,27) blob_id=%d level=%d\n",
                  rec.blob_id, GetLvl(world, CellCoord{11, 27}));
    }
  }

  if (ok) {
    p.painted_total += steps;
    return true;
  }

  // 失败回滚（不回滚level，Oracle保留主循环设置的level）
  {
    int bid = p.blob_id;
    int rollback_count = 0;
    ctx.grid.ForEachCell([&](const CellCoord& cc) {
      TouchW(world, cc);
      PaintWorkCell& rec = Rec(ctx, cc);
      if (rec.blob_id == bid) {
        ++rollback_count;
        rec.blob_id = 0;
        rec.flag4b  = false;
        world.SetTile(cc, 0);
        SetSub(world, cc, 0);
      }
    });
    std::printf("DEBUG: rollback cleaned %d cells with blob_id=%d\n", rollback_count, bid);
    if (fallCount > 0) {
      int fall_rollback = 0;
      ctx.grid.ForEachCell([&](const CellCoord& cc) {
        TouchW(world, cc);
        PaintWorkCell& rec = Rec(ctx, cc);
        if (rec.blob_id == bid - 1) {
          ++fall_rollback;
          rec.blob_id = 0;
          rec.flag4b  = false;
          world.SetTile(cc, 0);
          SetSub(world, cc, 0);
        }
      });
      std::printf("DEBUG: fall_rollback cleaned %d cells with blob_id=%d\n", fall_rollback, bid-1);
    }
  }
  long long rng_exit = ctx.rng->call_count();
  std::printf("DEBUG: River() exit false, rng=%lld consumed=%lld\n",
              rng_exit, rng_exit - rng_entry);
  std::fflush(stdout);
  return false;
}


void AddRiversAndLakes(RegionContext& ctx, RiverWorld& world, RiverParams& p) {
  p.blob_id += 1;

  if ((p.map_type == 3 || p.map_type == 4) && p.water_amount > 20) {
    bool ok = false;
    for (int i = 0; i < 10 && !ok; ++i) {
      CellCoord start{0, 0};
      ok = River(ctx, world, p, start, 0.0, false);
    }
    if (ok) p.blob_id += 1;
  }

  bool ok = false;
  for (int i = 0; i < 10 && !ok; ++i) {
    CellCoord start{0, 0};
    WaterBlobParams bp{p.water_amount, p.painted_total,
                       p.blob_id, p.cell_level};
    ok = WaterBlob(ctx, world, bp, start);
    p.painted_total = bp.painted_total;
    p.blob_id       = bp.blob_id;
  }
  if (ok) p.blob_id += 1;
}

}  // namespace rmg
}  // namespace ra2
