#include "water_blob.h"

#include <algorithm>
#include <cstdint>

#include "blob_heap.h"       // 与涂块核心共用的一基最小堆（下沉 0x5AD870）
#include "fast_math.h"       // FastSqrt（0x4CAC40）/ NormalDeviate（0x5980C0）
#include "place_tile.h"      // IsUndecidedTile（0x486380）
#include "scaled_random.h"   // PickBelowMul / Ftol / kInvUintMax

namespace ra2 {
namespace rmg {

namespace {

// 下面五个小工具与 regions.cpp 匿名命名空间里的那几个逐字相同。宁可抄一遍，
// 也不为了它们再开一个内部头文件把两个翻译单元绑在一起。

// 0x89F688，运行期由 0x49F2F0 填出：0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW。
const int kDeltaX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int kDeltaY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// 0x42D510 `CellStruct::operator+`：两个分量都是 int16 相加后再符号扩展。
inline CellCoord Step(const CellCoord& c, int dir) {
  return CellCoord{static_cast<std::int16_t>(c.x + kDeltaX[dir]),
                   static_cast<std::int16_t>(c.y + kDeltaY[dir])};
}

// 0x5657A0 `MapClass::GetCellAt` 的副作用：拿不到真实格子时把请求坐标写进
// 共享哑元的 +0x24（[0xABDC74]）。
inline void Touch(RegionWorld& w, const CellCoord& c) {
  if (!w.CellExists(c)) w.SetDummyCoord(c);
}

inline std::size_t WorkIdx(const RegionContext& ctx, const CellCoord& c) {
  const long long i = static_cast<long long>(c.y) * ctx.grid.side() + c.x;
  if (i < 0) return 0;
  const long long n = static_cast<long long>(ctx.work->size());
  return static_cast<std::size_t>(i >= n ? n - 1 : i);
}

inline PaintWorkCell& Rec(RegionContext& ctx, const CellCoord& c) {
  return (*ctx.work)[WorkIdx(ctx, c)];
}

inline bool IsOrigin(const CellCoord& c) { return c.x == 0 && c.y == 0; }

// 0x59CB97-0x59CBD7：等距变换全程用 int16 打包分量做加法，宽度照抄。
// 与 blob_paint.cpp 的 IsoFromReal 是同一串指令。
inline CellCoord IsoFromReal(int rx, int ry, int w) {
  const std::int16_t nx = static_cast<std::int16_t>(-rx);
  const std::int16_t ww = static_cast<std::int16_t>(w);
  const std::int16_t x0 = static_cast<std::int16_t>(rx + 1);
  const std::int16_t y0 = static_cast<std::int16_t>(ww + nx);
  const std::int16_t x =
      static_cast<std::int16_t>(x0 + static_cast<std::int16_t>(ry));
  const std::int16_t y =
      static_cast<std::int16_t>(static_cast<std::int16_t>(ry) + y0);
  return CellCoord{x, y};
}

constexpr double kWaterScale = 0.008;   // [0x7ED9F8]
constexpr double kWaterBias = 100.0;    // [0x7E2AC0]
constexpr double kMinBlob = 75.0;       // [0x7ED9F0]
constexpr double kHalf = 0.5;           // [0x7E1738]

// 优先级的三个系数在原版里是 **float** 常量，被 x87 一条 `fld dword` 抬成
// 扩展精度参与运算。这里按 float→double 的精确值写死，尾数不许自己凑。
constexpr double kJitter = 10.0;                    // [0x7ED768] = 10.0f
constexpr double kDistWeight = 0.5;                 // [0x7ED764] = 0.5f
constexpr double kRingWeight = 0.019999999552965164;  // [0x7ED76C] = 0.02f

}  // namespace

bool WaterBlob(RegionContext& ctx, WaterBlobWorld& world, WaterBlobParams& p,
               const CellCoord& start) {
  Randomizer& rng = *ctx.rng;
  NormalDeviate& normal = *ctx.normal;

  // ── 预算 0x59C92E-0x59C970 ────────────────────────────────────────────
  // ⚠️ WaterAmount == 0 时 eax 保持为「刚读出来的 WaterAmount」也就是 0，
  // 于是 budget = -painted_total，必然 <= 0x4B 而直接失败。这条支路走不通，
  // 但它是靠寄存器残值成立的，照写。
  int budget = 0;
  if (p.water_amount != 0) {
    double v = static_cast<double>(ctx.map_h);          // fild [ebx+0x184]
    v = v * static_cast<double>(ctx.map_w);             // fimul [ebx+0x180]
    v = v * static_cast<double>(p.water_amount);        // fimul [esp+0x50]
    v = v * kWaterScale;                                // 0x59C952
    v = v + kWaterBias;                                 // 0x59C958
    budget = Ftol(v);                                   // 0x59C95E
  }
  budget -= p.painted_total;                            // 0x59C963
  if (budget <= 0x4B) return false;                     // 0x59C970

  // ── 池与堆 0x59C976-0x59CA0B ──────────────────────────────────────────
  const int want = 2 * budget + 2;                      // lea edi,[eax+eax+2]
  const int cap = std::max(want, 100);                  // 0x59C97A / 0x59C9BD
  // 原版的节点池就是 malloc(cap*8)，**没有**任何上限检查：候选格多到装不下
  // 时它会写出界（0x59CFD0 `mov [ebx],eax`）。那是未定义行为，不可复刻，
  // 所以这里把池开得足够大让越界永不发生。堆那边的 `count+1 >= cap` 拒收
  // **是**可观测行为，照旧交给 MinHeap 复刻。
  const int side = ctx.grid.side();
  NodePool pool(cap + side * side + 16);
  MinHeap heap(&pool, cap);

  const std::size_t cells = ctx.work->size();

  // ── 全图清 +0x3C 0x59CA0B-0x59CA56 ───────────────────────────────────
  // ⚠️ 这一段在 start 分支**之前**，两条路都要走。
  ctx.grid.ForEachCell([&](const CellCoord& c) { Rec(ctx, c).queued = 0; });

  if (IsOrigin(start)) {
    // 「禁涂带」预处理 0x59CA79-0x59CADA。三段都是**线性**扫 side² 条记录，
    // 不是按地图迭代顺序——菱形外的槽位也一起清。
    for (std::size_t i = 0; i < cells; ++i) (*ctx.work)[i].flag44 = false;
    Erode(ctx, p.cell_level, 0, 2, -2);                 // 0x59CA92 push -2/2/0
    for (std::size_t i = 0; i < cells; ++i) {
      PaintWorkCell& w = (*ctx.work)[i];
      if (w.blob_id == 0 || w.blob_id == p.blob_id) {
        w.flag44 = true;                                // 0x59CACB
      } else if (w.blob_id == -2) {
        w.blob_id = 0;                                  // 0x59CAC3，但不放行
      }
    }
  } else {
    // 0x59CC81：河口挖湖时全图放行，禁涂带整套跳过。
    for (std::size_t i = 0; i < cells; ++i) (*ctx.work)[i].flag44 = true;
  }

  // ── 起点 0x59CADF-0x59CC74 ────────────────────────────────────────────
  CellCoord at = start;
  if (IsOrigin(start)) {
    int attempts = 0;                                   // 0x59CAE3
    for (;;) {
      const int rx = PickBelowMul(rng, ctx.grid.width());    // [0x87F8DC]
      const int ry = PickBelowMul(rng, ctx.grid.height());   // [0x87F8E0]
      at = IsoFromReal(rx, ry, ctx.grid.width());
      // ⚠️ 先加先判：第 200 次抽到的坐标即使合法也会被丢掉，
      // 而那两个随机数**照样消耗**。
      if (++attempts >= 0xC8) return false;             // 0x59CBEE
      if (Rec(ctx, at).blob_id != 0) continue;          // 0x59CC1B
      Touch(world, at);                                 // 0x59CC2D GetCellAt
      if (!IsUndecidedTile(world.IsoTile(at))) continue;   // 0x59CC39
      if (!Rec(ctx, at).flag44) continue;               // 0x59CC66
      break;
      // ⚠️ 这里原版还有第二次 `attempts >= 200 → return false`
      //（0x59CC6C / 0x59CC76），但上面那一句已经把 >= 200 全拦掉了，
      // 所以那个出口**永远到不了**。见 §二 的死代码记录。
    }
  }

  // ── 这一块要涂多大 0x59CCA7-0x59CD7D ─────────────────────────────────
  const int clamped_i = (budget < 0x4C) ? 0x4C : budget;   // 0x59CCAD
  const double clamped = static_cast<double>(clamped_i);
  // 0x2AAAAAAB / 0x55555556 两个免移位魔数除法，各配一次「向零取整」修正。
  double sigma = static_cast<double>(budget / 6);        // 0x59CCC2
  double mean = static_cast<double>(budget / 3);         // 0x59CCD8
  // 0x59CD02 / 0x59CD15：两条 fcomp 的方向照抄，NaN 走「不修正」那一支。
  if ((mean - sigma) > clamped || (mean + sigma) < kMinBlob) {
    sigma = (clamped - kMinBlob) * kHalf;                // 0x59CD22
    mean = sigma + kMinBlob;                             // 0x59CD36
  }
  double v;
  do {                                                   // 0x59CD40
    v = normal() * sigma + mean;
  } while (v < kMinBlob || v > clamped);                 // 0x59CD5E / 0x59CD6D
  const int this_budget = Ftol(v);                        // 0x59CD76

  // ── 起点入堆 0x59CD81-0x59CE32 ────────────────────────────────────────
  // 原版这里还把堆数组 storage[0..count] 清一遍再置 count = 0；此时堆本来
  // 就是空的，纯粹是编译器把「清堆」那段内联了，没有可观测效果。
  {
    const int node = pool.Alloc(at.x, at.y, 0.0f);        // 0x59CDA7，优先级 0.0f
    Rec(ctx, at).queued = p.blob_id;                      // 0x59CDD4
    if (node >= 0) heap.Push(node);                       // 0x59CDD8 起内联上浮
  }

  bool ok = true;                                         // 0x59C937 [esp+0x13] = 1
  int painted = 0;                                        // 0x59CCB0 [esp+0x1c] = 0
  int cur = heap.empty() ? -1 : heap.Pop();               // 0x59CE32

  // ── 主循环 0x59CE5E-0x59D14B ──────────────────────────────────────────
  if (this_budget > 0) {                                  // 0x59CE6A jle
    for (;;) {
      if (cur < 0) break;                                 // 0x59CE84
      if (!ok) break;                                     // 0x59CE90
      const CellCoord c{pool[cur].x, pool[cur].y};

      // ⚠️ 认领是**无条件**的：既不看这一格现在归谁，也不看砖是什么。
      // 主循环相信弹出来的都是入堆时验过的——而起点那一格根本没验过归属之外的事。
      Rec(ctx, c).blob_id = p.blob_id;                     // 0x59CEBA
      Touch(world, c);                                     // 0x59CEBE GetCellAt
      world.SetIsoTile(c, ctx.theater->water_set);          // 0x59CED3 [0xAA0738]
      world.SetSubTile(c, 0);                               // 0x59CED6

      for (int d = 0; d < 8; d += 2) {                      // 0x59D0EE add eax,2
        const CellCoord n = Step(c, d & 7);
        // 0x59CF32-0x59CF60：菱形四不等式，界外是 continue 不是失败。
        if (!ctx.grid.InMap(n)) continue;
        PaintWorkCell& wn = Rec(ctx, n);
        bool accepted = false;
        if (wn.blob_id == 0 && wn.queued != p.blob_id) {    // 0x59CF80 / 0x59CF8F
          Touch(world, n);                                  // 0x59CFA5
          if (IsUndecidedTile(world.IsoTile(n)) && wn.flag44) {
            // 距离是「起点 → 这一格」，起点全程不动（0x59BBC0 那个会漂移的
            // 引力中心，湖这边没有）。
            const int dx = at.x - n.x;                      // 0x59CFDA
            const int dy = at.y - n.y;                      // 0x59CFE1
            const int d2 = dy * dy + dx * dx;               // 0x59CFED
            const float dist =
                static_cast<float>(FastSqrt(static_cast<double>(d2)));
            const double u =
                static_cast<double>(rng.Random()) * kInvUintMax;   // [0x7ED898]
            // 0x59D02F-0x59D053：四次运算全在 x87 里按 PC=53 走，
            // 只有最后一次 fstp dword 才落成 float。不许自己合并。
            double prio = kJitter * u;
            prio = prio + kDistWeight * static_cast<double>(dist);
            prio = prio - kRingWeight * static_cast<double>(painted);
            const int node =
                pool.Alloc(n.x, n.y, static_cast<float>(prio));
            wn.queued = p.blob_id;                          // 0x59D05C
            if (node >= 0) heap.Push(node);                 // 0x59D05F 起内联上浮
            accepted = true;
          }
        }
        if (!accepted) {
          // 0x59D0D2：无主（0）或已经是自己的都放过，只有第三方才判失败。
          const int o = wn.blob_id;
          if (o != 0 && o != p.blob_id) ok = false;          // 0x59D0E5
        }
      }

      ++painted;                                            // 0x59D102
      cur = heap.empty() ? -1 : heap.Pop();                 // 0x59D107 起
      if (painted >= this_budget) break;                    // 0x59D147 jl
    }
  }

  // ── 收编堆里剩下的 0x59D14D-0x59D237 ─────────────────────────────────
  // ⚠️ 与主循环不一致：这一段**要查砖、要查归属、要查放行位**，任何一格不合格
  // 就整块判失败。主循环反而一路无条件涂。这个不对称是原版的，不要抹平。
  cur = heap.empty() ? -1 : heap.Pop();                     // 0x59D14D 起
  if (cur >= 0) {                                           // 0x59D17F
    for (;;) {
      if (!ok) break;                                       // 0x59D18B → 失败收尾
      const CellCoord c{pool[cur].x, pool[cur].y};
      Touch(world, c);                                      // 0x59D197
      PaintWorkCell& w = Rec(ctx, c);
      if (w.blob_id == 0 && IsUndecidedTile(world.IsoTile(c)) && w.flag44) {
        world.SetIsoTile(c, ctx.theater->water_set);         // 0x59D1DE
        world.SetSubTile(c, 0);                              // 0x59D1E1
        w.blob_id = p.blob_id;                               // 0x59D1EE
      } else {
        ok = false;                                          // 0x59D1F3
      }
      cur = heap.empty() ? -1 : heap.Pop();                  // 0x59D1F8 起
      ++painted;                                             // 0x59D228
      if (cur < 0) break;                                    // 0x59D233 jne
    }
  }

  // ── 成败判定 0x59D239-0x59D259 ───────────────────────────────────────
  if (ok) {
    if (painted <= 0x4B) {
      ok = false;                                            // 0x59D291
    } else if (painted <= this_budget / 4) {                 // cdq/and 3/add/sar 2
      ok = false;
    }
  }

  // ── 成功收尾 0x59D25B-0x59D3B1 ───────────────────────────────────────
  // 两次 `start == (0,0)` 判定是原版分开写的（0x59D263 与 0x59D2EA），
  // 中间夹着岸线平滑；照抄这个形状，别合成一个 if。
  if (ok && IsOrigin(start)) {
    ok = world.SmoothShoreline(p.blob_id);                    // 0x57A0C0(blobId, 0)
  }
  if (ok && IsOrigin(start)) {
    // 既是「这块水有没有和第三方区域贴上」的隔离检测，也顺手把外围一圈收归己有。
    ok = Dilate(ctx, p.blob_id, 1, 0, 0, 0x200, 0x200, false, 0);   // 0x59D323
    if (ok) {
      // 0x59D343-0x59D3AA：本块里砖号还是 0 / 0xFFFF 的格子刷成绿地。
      ctx.grid.ForEachCell([&](const CellCoord& c) {
        if (Rec(ctx, c).blob_id != p.blob_id) return;         // 0x59D384
        const int t = world.IsoTile(c);
        if (t != 0 && t != 0xFFFF) return;                    // 0x59D38B / 0x59D393
        world.SetIsoTile(c, ctx.theater->green_tile);          // 0x59D39B [0xAA0E18]
      });
      ok = true;                                              // 0x59D3AC
    }
  }

  if (ok) {
    p.painted_total += painted;                               // 0x59D2D5
    return true;
  }

  // ── 回滚 0x59D3B6-0x59D42A ───────────────────────────────────────────
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    PaintWorkCell& w = Rec(ctx, c);
    if (w.blob_id != p.blob_id) return;                       // 0x59D3FF
    w.blob_id = 0;                                            // 0x59D401
    w.flag4b = false;                                         // 0x59D404
    world.SetIsoTile(c, 0);                                   // 0x59D408
    world.SetSubTile(c, 0);                                   // 0x59D40B
    world.SetLevel(c, p.cell_level);                          // 0x59D418 +0x30C
  });
  return false;
}

}  // namespace rmg
}  // namespace ra2
