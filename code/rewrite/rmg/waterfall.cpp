#include "waterfall.h"

#include <cstdint>

#include "scaled_random.h"

namespace ra2 {
namespace rmg {

namespace {

// 每一处坐标写入都是 16 位存（`mov word [esp+..], ax`）。循环计数器却是
// 32 位的，所以「先按 int 算、再截成 int16 落地」这一步不能省。
inline int S(int v) { return static_cast<std::int16_t>(v); }
inline CellCoord C(int x, int y) { return CellCoord{S(x), S(y)}; }

// 0x5657A0 GetCellAt 的副作用：拿不到真实格子就把请求坐标写进共享哑元 +0x24。
// 本函数里每一次访问格子前都要走一遍——原版调了几次就写几次。
inline void Touch(WaterfallWorld& w, const CellCoord& c) {
  if (!static_cast<RegionWorld&>(w).CellExists(c)) {
    static_cast<RegionWorld&>(w).SetDummyCoord(c);
  }
}

// RegionWorld / TileWorld 两边都声明了这几个，非限定名查找会二义。
inline void SetSub(WaterfallWorld& w, const CellCoord& c, int v) {
  static_cast<RegionWorld&>(w).SetSubTile(c, v);
}
inline int Lvl(const WaterfallWorld& w, const CellCoord& c) {
  return static_cast<const RegionWorld&>(w).Level(c);
}
inline void SetLvl(WaterfallWorld& w, const CellCoord& c, int v) {
  static_cast<RegionWorld&>(w).SetLevel(c, v);
}

inline PaintWorkCell& Rec(RegionContext& ctx, const CellCoord& c) {
  return (*ctx.work)[static_cast<std::size_t>(ctx.grid.WorkIndex(c))];
}

// 四处地方都是这一串：`rem & 0x80000001`，负数再 `dec / or 0xfffffffe / inc`
// 折回来。rem 在本函数里恒非负，所以它退化成 `rem % 2 == 0`；写成取模是
// 因为 MSVC 生成这一串的源头本来就是 `rem % 2`。
inline bool Even(int rem) { return (rem % 2) == 0; }

}  // namespace

bool PlaceWaterfall(RegionContext& ctx, WaterfallWorld& world,
                    const WaterfallParams& p, const CellCoord& a,
                    const CellCoord& b, int dir, bool* ok_out, double* x,
                    double* y) {
  Randomizer& rng = *ctx.rng;
  const MapGrid& grid = ctx.grid;

  // 0x59E754：进场就写 0。之后没有任何一条早退路径再碰它，
  // 于是「净空扫描否决」与「压根没走到」在调用方眼里长得一模一样。
  *ok_out = false;

  // ── 第 2 步：按 dir 拼出五个矩形。跳转表 0x5A0050 ──
  // dir 只认 0/2/4/6。奇数或 > 6 一律落到 0x59E9FA，十四个栈槽保持
  // 序言里清出来的 0（0x59E76A-0x59E79D），后面每个循环都空转。
  int clear_x0 = 0, clear_xn = 0, clear_y0 = 0, clear_yn = 0;
  Rect w1{};        // [esp+0x60..0x6c] 上游水面
  Rect plateau{};   // [esp+0x70..0x7c] 台地
  Rect w2{};        // [esp+0x50..0x5c] 下游水面
  CellCoord origin{0, 0};   // [esp+0x3c] AngularGrow 的极点

  switch (dir) {
    case 0: {   // 0x59E7AE，河往北走
      const int ax = S(a.x), ay = S(a.y), bx = S(b.x);
      clear_x0 = ax - 2;   clear_xn = (bx - ax) + 5;
      clear_y0 = ay - 12;  clear_yn = 12;
      w1 = Rect{ax, ay - 4, (bx - ax) + 1, 4};
      plateau = Rect{0, ay - 4, 0x200, 0x200 - (ay - 4)};
      origin = C(ax, ay - 4);
      w2 = Rect{ax, ay - 12, (bx - ax) + 1, 8};
      break;
    }
    case 2: {   // 0x59E8CE，河往东走
      const int ax = S(a.x), ay = S(a.y), by = S(b.y);
      clear_x0 = ax + 1;   clear_xn = 12;
      clear_y0 = ay - 2;   clear_yn = (by - ay) + 5;
      w1 = Rect{ax + 1, ay, 4, (by - ay) + 1};
      plateau = Rect{0, 0, ax + 4, 0x200};
      origin = C(ax + 5, ay);
      w2 = Rect{ax + 5, ay, 8, (by - ay) + 1};
      break;
    }
    case 4: {   // 0x59E842，河往南走
      const int ax = S(a.x), bx = S(b.x), by = S(b.y);
      clear_x0 = bx - 2;   clear_xn = (ax - bx) + 5;
      clear_y0 = by + 1;   clear_yn = 12;
      w1 = Rect{bx, by + 1, (ax - bx) + 1, 4};
      plateau = Rect{0, 0, 0x200, by + 4};
      origin = C(bx, by + 1);
      w2 = Rect{bx, by + 5, (ax - bx) + 1, 8};
      break;
    }
    case 6: {   // 0x59E962，河往西走
      const int ay = S(a.y), bx = S(b.x), by = S(b.y);
      clear_x0 = bx - 12;  clear_xn = 12;
      clear_y0 = by - 2;   clear_yn = (ay - by) + 5;
      w1 = Rect{bx - 4, by, 4, (ay - by) + 1};
      plateau = Rect{bx - 4, 0, 0x200 - (bx - 4), 0x200};
      origin = C(bx - 4, by);
      w2 = Rect{bx - 12, by, 8, (ay - by) + 1};
      break;
    }
    default:
      break;
  }

  // ── 第 3 步：净空扫描 0x59E9FA-0x59EACD ──
  // 全函数唯一的「此处放不下」判据，而且一个随机数都不取——被它否决时
  // RNG 流完全不受扰动，河那边可以放心地当作没发生过。
  {
    const int y_end = clear_y0 + clear_yn;
    const int x_end = clear_x0 + clear_xn;
    for (int yy = clear_y0; yy < y_end; ++yy) {
      for (int xx = clear_x0; xx < x_end; ++xx) {
        const CellCoord c = C(xx, yy);
        if (!grid.InMap(c)) continue;
        // 0x59EA56：工作数组为空也算失败
        if (ctx.work == nullptr) return true;
        if (Rec(ctx, c).blob_id != 0) return true;
        Touch(world, c);
        if (!IsUndecidedTile(world.Tile(c))) return true;
      }
    }
  }

  // ── 第 4 / 9 步共用：把一片矩形涂成水 ──
  // 每格 2 次拒绝采样：砖号 PickBelow(6)（比例常数 [0x7EDA40] = 6/(2^32-1)）
  // 与子格 PickBelow(4)（[0x7EDA38] = 4/(2^32-1)）。两次 GetCellAt 也照做。
  // ⚠️ 认领是无条件的：不看这一格原来归谁，也不看砖是什么。
  const auto paint_water = [&](const Rect& r) {
    const int y_end = r.y + r.h;
    const int x_end = r.x + r.w;
    for (int yy = r.y; yy < y_end; ++yy) {
      for (int xx = r.x; xx < x_end; ++xx) {
        const CellCoord c = C(xx, yy);
        if (!grid.InMap(c)) continue;
        const int t = PickBelow(rng, 6);
        Touch(world, c);
        world.SetTile(c, ctx.theater->water_set + t);
        const int s = PickBelow(rng, 4);
        Touch(world, c);
        SetSub(world, c, s);
        if (ctx.work != nullptr) Rec(ctx, c).blob_id = p.blob_id;
      }
    }
  };

  paint_water(w1);

  // ── 第 5-7 步：台地三连（0x59EC65 / 0x59EC86 / 0x59ECCE）──
  // 短路串联，任何一环失败就整段收工。⚠️ 上面已经涂下去的水不回滚。
  // 0x3B449BA6 就是 0.003f。
  bool ok = AngularGrow(ctx, p.blob_stamp, 0.003f, plateau, origin, false);
  if (ok) {
    ok = world.SmoothShoreline(p.blob_stamp);   // 0x57A0C0(stamp, 0)
    if (ok) {
      ok = Dilate(ctx, p.blob_stamp, 2, plateau.x, plateau.y, plateau.w,
                  plateau.h, false, 0);
      if (ok) {
        // ── 第 8 步：整体抬升 0x59ECDF-0x59ED4B ──
        // 走地图迭代器，凡是归 stamp 的格子层高 +4。⚠️ 工作数组为空时
        // 比较值取 -1（0x59ED0A），于是一格都抬不起来。
        grid.ForEachCell([&](const CellCoord& c) {
          const int owner = (ctx.work == nullptr) ? -1 : Rec(ctx, c).blob_id;
          if (owner != p.blob_stamp) return;
          SetLvl(world, c, Lvl(world, c) + 4);
        });

        // ── 第 9 步：下游水面。它留在低处，于是与上游之间出现一道 4 层的坎 ──
        paint_water(w2);

        // ── 第 10 步：贴四块瀑布砖。跳转表 0x5A006C ──
        // 出参 local_111 先置 1（0x59EED1），四次 PlaceTile 共用它，
        // 但之后谁也不读——贴不上也不会让 ok 变 false。
        bool tile_ok = true;

        // 一次贴砖：砖表取 [0xA8ED2C] + (base + slot)*4，落点走 SetTargetCell。
        const auto place = [&](int base, int slot, const CellCoord& at,
                               int level) {
          const TileType* t = world.TileByIndex(base + slot);
          if (t == nullptr) return;   // 原版这里会拿着空指针继续跑
          PlaceTileParams tp;
          tp.allow_lo = 0;
          tp.allow_hi = 0x10000;
          tp.base_level = level;
          tp.blob_id = p.blob_stamp;
          tp.ok = &tile_ok;
          tp.no_blob_tracking = false;
          PlaceTile(world, grid, *ctx.theater, *t, at, tp);
        };

        // 坎两端各有一格「垫脚」：砖清成 0xFFFF、层高补回 +4、子格归零，
        // 并归到 stamp 名下。是贴砖段里唯一带菱形判定的写入。
        const auto ledge = [&](const CellCoord& c) {
          if (!grid.InMap(c)) return;
          Touch(world, c);
          world.SetTile(c, 0xFFFF);
          Touch(world, c);
          SetLvl(world, c, Lvl(world, c) + 4);
          Touch(world, c);
          SetSub(world, c, 0);
          if (ctx.work != nullptr) Rec(ctx, c).blob_id = p.blob_stamp;
        };

        switch (dir) {
          case 0: {   // 0x59FB3F，WaterfallNorth [0xAA10A0]
            const int base = ctx.theater->waterfall_north;
            const int X0 = w1.x - 2, Y0 = w1.y - 2;
            Touch(world, C(X0, w1.y - 4));
            const int level = Lvl(world, C(X0, w1.y - 4));
            place(base, 0, C(X0, Y0), level);
            place(base, 3, C(X0 + (w2.w + 2), Y0), level);
            Touch(world, C(X0 + 1, Y0));
            world.SetTile(C(X0 + 1, Y0), 0xFFFF);
            Touch(world, C(X0 + (w2.w + 2), Y0));
            world.SetTile(C(X0 + (w2.w + 2), Y0), 0xFFFF);
            for (int i = 0; i < w2.w + 2; ++i) {
              const CellCoord c = C(X0 + (i + 1), Y0);
              Touch(world, c);
              SetLvl(world, c, Lvl(world, c) - 4);
            }
            ledge(C(X0 - 1, Y0 + 1));
            ledge(C(X0 + (w2.w + 4), Y0 + 1));
            for (int i = 0, rem = w2.w; i < w2.w;) {
              // ⚠️ 0x59FEF9 的 `lea edx,[esi+2]` 在分支之前，
              // 所以两条支路的 X 都是 i+2，不是「奇数走 i+1」。
              const CellCoord c = C(X0 + (i + 2), Y0 + 1);
              if (Even(rem)) {
                place(base, 2, c, level);
                i += 2;
                rem -= 2;
              } else {
                place(base, 1, c, level);
                i += 1;
                rem -= 1;
              }
            }
            break;
          }
          case 2: {   // 0x59EEE8，WaterfallEast [0xAA073C]
            const int base = ctx.theater->waterfall_east;
            const int X0 = w2.x, Y1 = w2.y + w2.h;
            Touch(world, C(X0 + 2, Y1));
            const int level = Lvl(world, C(X0 + 2, Y1));
            place(base, 0, C(X0, Y1), level);
            place(base, 3, C(X0, Y1 - (w2.h + 2)), level);
            // ⚠️ dir 2 / dir 4 没有那两格 0xFFFF，也没有 -4 的那圈回压；
            // 只有 dir 0 / dir 6 有。不是漏抄，是原版就这么不对称。
            ledge(C(X0, Y1 + 2));
            ledge(C(X0, Y1 - (w2.h + 3)));
            for (int i = 0, rem = w2.h; i < w2.h;) {
              if (Even(rem)) {
                place(base, 2, C(X0, Y1 - (i + 2)), level);
                i += 2;
                rem -= 2;
              } else {
                place(base, 1, C(X0, Y1 - (i + 1)), level);
                i += 1;
                rem -= 1;
              }
            }
            break;
          }
          case 4: {   // 0x59F77C，WaterfallSouth [0xAA1050]
            const int base = ctx.theater->waterfall_south;
            const int X0 = w2.x - 2, Y0 = w2.y;
            Touch(world, C(X0, Y0 + 2));
            const int level = Lvl(world, C(X0, Y0 + 2));
            place(base, 0, C(X0, Y0), level);
            place(base, 3, C(X0 + (w2.w + 2), Y0), level);
            ledge(C(X0 - 1, Y0));
            ledge(C(X0 + (w2.w + 4), Y0));
            for (int i = 0, rem = w2.w; i < w2.w;) {
              // 同 dir 0：两条支路都是 i+2
              const CellCoord c = C(X0 + (i + 2), Y0);
              if (Even(rem)) {
                place(base, 2, c, level);
                i += 2;
                rem -= 2;
              } else {
                place(base, 1, c, level);
                i += 1;
                rem -= 1;
              }
            }
            break;
          }
          case 6: {   // 0x59F2C4，WaterfallWest [0xABB110]
            const int base = ctx.theater->waterfall_west;
            const int X0 = w1.x - 2, Y1 = w2.y + w2.h;
            Touch(world, C(X0 - 2, Y1));
            const int level = Lvl(world, C(X0 - 2, Y1));
            place(base, 0, C(X0, Y1), level);
            place(base, 3, C(X0, Y1 - (w2.h + 2)), level);
            Touch(world, C(X0, Y1 - 1));
            world.SetTile(C(X0, Y1 - 1), 0xFFFF);
            // ⚠️ 0x59F413 `mov edx,[esp+0x58]` 取的是 w2.w（恒为 8），
            // 而这条竖着的坎跨度是 w2.h。四个朝向里只有它拿错了字段——
            // 原版的复制粘贴手误，照抄不改。
            Touch(world, C(X0, Y1 + (w2.w + 2)));
            world.SetTile(C(X0, Y1 + (w2.w + 2)), 0xFFFF);
            for (int i = 0; i < w2.h + 2; ++i) {
              const CellCoord c = C(X0, Y1 - i);
              Touch(world, c);
              SetLvl(world, c, Lvl(world, c) - 4);
            }
            ledge(C(X0 + 1, Y1 + 2));
            ledge(C(X0 + 1, Y1 - (w2.h + 3)));
            for (int i = 0, rem = w2.h; i < w2.h;) {
              if (Even(rem)) {
                place(base, 2, C(X0 + 1, Y1 - (i + 2)), level);
                i += 2;
                rem -= 2;
              } else {
                place(base, 1, C(X0 + 1, Y1 - (i + 1)), level);
                i += 1;
                rem -= 1;
              }
            }
            break;
          }
          default:
            break;   // 0x59FFD4
        }
      }
    }
  }

  // ── 第 11-12 步：0x5A0008-0x5A0035 ──
  // 成功了就把调用方的游标直接搬走 12 格，河道在这里断开重画。
  // 两张表落在刚才那两个矩形的栈槽上（局部变量复用），下标 dir/2 是
  // `cdq/sub/sar 1` 的有符号除法。
  const int dx[4] = {0, 0xC, 0, -0xC};
  const int dy[4] = {-0xC, 0, 0xC, 0};
  if (ok) {
    const int k = dir / 2;
    // dir 只可能是 0/2/4/6（调用点 0x59DD1C），k 落在 [0,4)；
    // 原版没有这道保护，dir >= 8 会读到栈上别的槽。那属于不可复现的 UB。
    if (k >= 0 && k < 4) {
      *x = static_cast<double>(dx[k]) + *x;
      *y = static_cast<double>(dy[k]) + *y;
    }
  }

  *ok_out = ok;
  return ok;
}

}  // namespace rmg
}  // namespace ra2
