#include "rivers.h"

#include <cstdint>

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
  Randomizer& rng   = *ctx.rng;
  NormalDeviate& nd = *ctx.normal;
  const MapGrid& g  = ctx.grid;
  // 0x59D5BA 起：起点采样与入口坐标全读 [0x87F8DC]/[0x87F8E0]（MapRect
  // W/H = ms.width+4 / ms.height+12），**不是** MapSeed +0x180/+0x184
  // （那是 WaterBlob 预算的分母，见 water_blob.cpp）。golden 小地图里
  // 两者相同才没暴露，真实 70x70 地图上差 4/12 格。
  const int W = g.width();
  const int H = g.height();

  CellCoord cur = start;
  double ang = angle;
  // 原版把 is_branch 存在栈参数槽 [ebp+0x14] 里，支流触发时写 1——
  // 之后瀑布与支流都因同一标志被禁（0x59DCA1 / 0x59DE83 读它）。
  bool branch_mode = is_branch;

  if (cur.x == 0 && cur.y == 0) {
    // 0x59D5BA / 0x59D602 / 0x59D64F：三个 PickBelow，不是取模！
    //   dir = ftol(r * (4/(2^32-1)))       折叠常数 0x7EDA38，> 3 重抽
    //   rx  = ftol(r * W * kInvUintMax)    两次乘法，> W-1 重抽
    //   ry  = ftol(r * H * kInvUintMax)
    // 取模与 scaled 对同一 r 给出不同值（dir 取的是 r 的高 2 位）——
    // 这里用错会让起点、初始角、整个 RNG 消耗模式全部错位。
    int dir = PickBelow(rng, 4);
    int rx  = PickBelowMul(rng, W);
    int ry  = PickBelowMul(rng, H);

    // 0x59D67B-0x59D7B0：TruncNormal(nd, mu=M, sigma=π/6, lo=M-π/4, hi=M+π/4)。
    // M = 7π/4 - dir·π/2（0x7EDA30 / 0x7E2820 两个 double 常数）。
    constexpr double kPi4 = 0.7853981633974483;
    constexpr double kPi6 = 0.5235987755982988;
    double M  = 5.497787143782138 -
                static_cast<double>(dir) * 1.5707963267948966;
    double HI = M + kPi4;
    double LO = M - kPi4;
    double sigma = kPi6, mean = M;
    // 死分支：恒不成立，照抄
    if (HI < M - kPi6 || M + kPi6 < LO) {
      sigma = (HI - LO) * 0.5;
      mean  = sigma + LO;
    }
    double ang_sample;
    do {
      ang_sample = nd();
      ang = ang_sample * sigma + mean;
    } while (ang < LO || ang > HI);

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

  // 河宽与跌水阈值
  // 0x59D84C-0x59D871：wmax = ftol(max(water*0.07, 1.0))
  double wf = static_cast<double>(p.water_amount) * 0.07;
  if (wf < 1.0) wf = 1.0;
  int wmax  = Ftol(wf);
  // 0x59D886-0x59D8B6：width = ftol(r*wmax*kInvUintMax + 1.0)，> wmax 重抽
  // ——与 PickRange(1, wmax) 同义（两次乘法）。
  int width = PickRange(rng, 1, wmax);
  int half  = width / 2;
  int widthLo = width - half;
  int widthHi = width + half;
  double curWidth = static_cast<double>(width);
  // 0x59D8F7-0x59D925：fallThreshold = ftol(r*(91*kInvUintMax) + 35.0)，
  // > 125 重抽。⚠️ 这里是**折叠常数**（0x7EDA18 = 91/(2^32-1)）一次乘法，
  // 与 PickRange 的 `r*span*kInv` 两次乘法在末位会分岔，不能通用。
  int fallThreshold;
  do {
    fallThreshold = Ftol(
        static_cast<double>(rng.Random()) * 2.1187588577435256e-08 + 35.0);
  } while (fallThreshold > 125);

  // 0x59D956-0x59D974：游标是**格子中心**（cur + 0.5），不是格子角。
  // 用整数坐标会让整条河的横截面偏移半格，画出的格子与原版错开。
  double x = static_cast<double>(cur.x) + 0.5;
  double y = static_cast<double>(cur.y) + 0.5;
  const double ang0 = ang;

  bool ok        = true;
  int  steps     = 0;
  int  fallCount = 0;
  double u;

  CellCoord crossA{}, crossB{};

  do {
    // 0x59D9A3-0x59DA03：本轮的起点有效性检查——游标出菱形或 ok 已被
    // 上一轮画格置假时，**立即退出主循环**（不是继续转到 u < 0.005）。
    if (!InMap(g, CellCoord{static_cast<std::int16_t>(Ftol(x)),
                            static_cast<std::int16_t>(Ftol(y))}) ||
        !ok) {
      break;
    }

    double c = Cos(ang);
    double s = Sin(ang);
    // 0x59DA32-0x59DA42：`n = ftol(curWidth + 0.5)`——带半格进位的
    // 四舍五入，不是直接截断。curWidth=1.5 时 n=2 而不是 1。
    int n = Ftol(curWidth + 0.5);

    double xL = x - static_cast<double>(n - 1) * s * 0.5;
    double yL = y - static_cast<double>(n - 1) * c * 0.5;
    const double xL0 = xL, yL0 = yL;  // 横截面起始端（瀑布 b 参数用）

    bool flagA = true, flagB = true;

    // 画格循环（0x59DAB9）：xL/yL 是**累加式**步进（每格 xL += s），
    // 与 `xL0 + i*s` 在浮点上不同（累加有累积误差），必须照抄。
    for (int i = 0; i < n; ++i) {
      const int cx = Ftol(xL);
      const int cy = Ftol(yL);
      const CellCoord cc{static_cast<std::int16_t>(cx),
                         static_cast<std::int16_t>(cy)};
      if (InMap(g, cc)) {
        TouchW(world, cc);
        PaintWorkCell& rec = Rec(ctx, cc);
        const int bid = rec.blob_id;
        if (bid == 0) {
          if (IsUndecidedTile(world.Tile(cc))) {
            // 涂格：只写 blob_id/tile/subtile，**不写 level**
            rec.blob_id = p.blob_id;
            world.SetTile(cc, ctx.theater->water_set);
            SetSub(world, cc, 0);
          } else {
            // 空地但砖已定（0x59DB7B 路径）：同样算碰撞失败
            ok = false;
          }
        } else if (bid != p.blob_id) {
          ok = false;  // 撞第三方地盘
        } else {
          // 自己的地盘再涂一遍（tile 恢复成水砖）
          world.SetTile(cc, ctx.theater->water_set);
          SetSub(world, cc, 0);
        }
      }
      // flagA/flagB 逐格检查（0x59DC1B）：横截面沿 x/y 方向步进是否
      // 换格。越界格同样参与检查。
      if (Ftol(xL) != Ftol(xL + s)) flagA = false;
      if (Ftol(yL) != Ftol(yL + c)) flagB = false;
      xL += s;
      yL += c;
    }

    // 瀑布坐标（0x59DD5E-0x59DDB1）：a = 累加后的末端（xL 已是 xL0+n*s），
    // b = 起始端 xL0。
    crossA = CellCoord{static_cast<std::int16_t>(Ftol(xL - s)),
                       static_cast<std::int16_t>(Ftol(yL - c))};
    crossB = CellCoord{static_cast<std::int16_t>(Ftol(xL0)),
                       static_cast<std::int16_t>(Ftol(yL0))};

    // 方向判断（0x59DC7D）：|c| < |s| → flagB = 0，否则 flagA = 0
    const double ac = c < 0.0 ? -c : c;
    const double as = s < 0.0 ? -s : s;
    if (ac < as) flagB = false; else flagA = false;

    // 瀑布（0x59DCA1）——在游标推进**之前**。
    // ⚠️ branch_mode 是「本河是否已生支流」的可变标志（原版 [ebp+0x14]），
    // 支流触发后这里与下面的支流检查都会被禁。
    if (!branch_mode && (flagA || flagB) && fallCount < 1) {
      const int dang  = Ftol(ang - ang0);
      const int adang = dang < 0 ? -dang : dang;
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

    // 游标推进（0x59DE18）
    x += c;
    y -= s;

    // 支流（0x59DE44，约 1% 概率）
    {
      const uint32_t ru = static_cast<uint32_t>(rng.Random());
      const double ub = static_cast<double>(ru) * kInvUintMax;
      if (ub < 0.01 && ok && !branch_mode && fallCount == 0) {
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
        branch_mode = true;  // 0x59DEA5：一条河最多一条支流
        // 0x59DF6E-0x59DF80：支流起点 = 画格横截面**末端**（xL/yL 累加后），
        // 不是推进后的游标 (x,y)——step6 处游标在 (32,17)、横截面末端在
        // (34,17)，两者差 2 格，用错会让支流整体偏移。
        const CellCoord bs{static_cast<std::int16_t>(Ftol(xL)),
                           static_cast<std::int16_t>(Ftol(yL))};
        // 0x59DFA2：ok = 递归返回值——递归失败时主河立即判负，
        // 下一轮循环开头 !ok 直接 break（不接收返回值会让主河多跑数十步）。
        ok = River(ctx, world, p, bs, bang, true);
      }
    }

    // 角度游走（0x59DFA6，步数 > 5 后）：
    // TruncNormal(nd, mu=0, sigma=π/10, lo=ang0-π/2-ang, hi=ang0+π/2-ang)
    // ——窗口是**绝对角度** [ang0-π/2, ang0+π/2] 随 ang 漂移，不是 ±π/10。
    if (steps > 5) {
      constexpr double kPi2  = 1.5707963267948966;
      constexpr double kPi10 = 0.3141592653589793;
      double dLO = ang0 - kPi2 - ang;
      double dHI = ang0 + kPi2 - ang;
      double dSig = kPi10, dMean = 0.0;
      if (dHI < dMean - kPi10 || dMean + kPi10 < dLO) {
        dSig  = (dHI - dLO) * 0.5;
        dMean = dSig + dLO;
      }
      double da;
      do { da = nd() * dSig + dMean; } while (da < dLO || da > dHI);
      ang += da;
    }

    // 宽度游走（0x59E08B，half > 0 才调整）
    if (half > 0) {
      const double wLO = static_cast<double>(widthLo) - curWidth;
      const double wHI = static_cast<double>(widthHi) - curWidth;
      double wSig = 0.5, wMean = 0.0;
      if (wHI < wMean - 0.5 || wMean + 0.5 < wLO) {
        wSig  = (wHI - wLO) * 0.5;
        wMean = wSig + wLO;
      }
      double dw;
      do { dw = nd() * wSig + wMean; } while (dw < wLO || dw > wHI);
      curWidth += dw;
    }

    ++steps;
    const uint32_t ru2 = static_cast<uint32_t>(rng.Random());
    u = static_cast<double>(ru2) * kInvUintMax;
  } while (u >= 0.005);

  // 步数不足 40 → 失败（0x59E1A8）
  if (steps < 40) ok = false;

  // 河口挖湖（0x59E209，需要 ok；支流同样挖）
  if (u < 0.005 && ok) {
    const CellCoord mouth{static_cast<std::int16_t>(Ftol(x)),
                          static_cast<std::int16_t>(Ftol(y))};
    if (InMap(g, mouth)) {
      WaterBlobParams bp{p.water_amount, p.painted_total,
                         p.blob_id, p.cell_level};
      bool lake_ok = WaterBlob(ctx, world, bp, mouth);
      p.painted_total = bp.painted_total;
      p.blob_id       = bp.blob_id;
      if (!lake_ok) ok = false;
    }
  }

  // 非支流收尾（0x59E239-0x59E316）：
  // SmoothShoreline → Dilate(1, flag=false) → 绿草刷图
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

  // 峡谷 / 坡道（0x59E318-0x59E47C）。
  //   is_branch 或 fallCount!=0 或 !ok 或 level!=4 → 跳过峡谷块。
  //   峡谷成功置 flagB_flag=1，之后收尾 Dilate(2) 被跳过、p.cell_level += 4。
  bool canyon_done = false;
  if (fallCount == 0 && ok && p.cell_level == 4 && !is_branch) {
    const uint32_t ru3 = static_cast<uint32_t>(rng.Random());
    u = static_cast<double>(ru3) * kInvUintMax;
    if (u < 0.7) {
      Rect full{0, 0, 0x200, 0x200};
      if (AngularGrow(ctx, p.blob_id, 0.01f, full, cur, true)) {
        ok = Dilate(ctx, p.blob_id, 6, 0, 0, 0x200, 0x200, false, 0);
        if (ok) {
          int bid = p.blob_id;
          ctx.grid.ForEachCell([&](const CellCoord& cc) {
            TouchW(world, cc);
            if (Rec(ctx, cc).blob_id != bid) {
              SetLvl(world, cc, GetLvl(world, cc) + 4);
            }
          });
          canyon_done = true;
        }
      } else {
        ok = false;  // AngularGrow 失败 → 直接回滚
      }
    }
  }

  // 公共收尾（0x59E47C / 0x59E4BA）：
  //   fallCount > 0 → Dilate(2, flag=true, level=p.cell_level)
  //   fallCount == 0 && !canyon_done → Dilate(2, flag=false, level=0)
  //   is_branch != 0 时 0x59E342 / 0x59E474 直接跳到成功出口，不跑这段。
  if (!is_branch && ok) {
    if (fallCount > 0) {
      ok = Dilate(ctx, p.blob_id, 2, 0, 0, 0x200, 0x200, true,
                  p.cell_level);
    } else if (!canyon_done) {
      ok = Dilate(ctx, p.blob_id, 2, 0, 0, 0x200, 0x200, false, 0);
    }
  }

  if (ok) {
    if (canyon_done) p.cell_level += 4;
    p.painted_total += steps;
    return true;
  }

  // 失败回滚（0x59E538）：blob_id == p.blob_id 的格子清空，
  // **level 写回 p.cell_level**（不是保留原值）；fallCount > 0 时
  // 再清 blob_id == p.blob_id - 1 的那一块。
  {
    const int bid = p.blob_id;
    const int rollback_level = p.cell_level;
    ctx.grid.ForEachCell([&](const CellCoord& cc) {
      TouchW(world, cc);
      PaintWorkCell& rec = Rec(ctx, cc);
      if (rec.blob_id == bid) {
        rec.blob_id = 0;
        rec.flag4b  = false;
        world.SetTile(cc, 0);
        SetSub(world, cc, 0);
        SetLvl(world, cc, rollback_level);
      }
    });
    if (fallCount > 0) {
      ctx.grid.ForEachCell([&](const CellCoord& cc) {
        TouchW(world, cc);
        PaintWorkCell& rec = Rec(ctx, cc);
        if (rec.blob_id == bid - 1) {
          rec.blob_id = 0;
          rec.flag4b  = false;
          world.SetTile(cc, 0);
          SetSub(world, cc, 0);
          SetLvl(world, cc, rollback_level);
        }
      });
    }
  }
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
