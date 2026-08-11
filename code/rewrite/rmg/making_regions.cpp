// 阶段 4「Making regions」核心：重算成员/切分 → 推倒重建 → 邻区表 → 开通路。
// 坡道/架桥的建造函数在 ramps.cpp，悬崖重刷在 cliff_work.cpp。
//
// 取证（gamemd.exe）：
//   0x58EBC0 重算成员 + 切分（含 0x58D620 的 4 个取数点）
//   0x58EF10 推倒重建（FillNotch 第一遍 + SeedRegion + 邻区表 + 开通路）
//   0x58F0C0 邻区表（不取随机数）
//   0x5905D0 开通路（3 个取数点）
// 详见 docs/random-map/stage-making-regions.md §二 ~ §五。
#include "making_regions.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "blob_heap.h"      // NodePool / MinHeap（切分的扩散堆）
#include "cliff_work.h"     // FillNotch（RebuildRegions 第一遍）
#include "ramps.h"          // BuildRamp / BuildBridge（MakeConnections）
#include "scaled_random.h"
#include "zones.h"          // IsWithinUsableArea / MapRect

namespace ra2 {
namespace rmg {
namespace {

// 0x89F688：0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW。
const int kDeltaX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int kDeltaY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// 0x42D510 `CellStruct::operator+`：两个分量都是 int16 相加后符号扩展。
inline CellCoord Step(const CellCoord& c, int dir) {
  return CellCoord{static_cast<std::int16_t>(c.x + kDeltaX[dir]),
                   static_cast<std::int16_t>(c.y + kDeltaY[dir])};
}

inline std::size_t WorkIdx(const RegionContext& ctx, const CellCoord& c) {
  const long long i =
      static_cast<long long>(c.y) * ctx.grid.side() + c.x;
  if (i < 0) return 0;
  const long long n = static_cast<long long>(ctx.work->size());
  return static_cast<std::size_t>(i >= n ? n - 1 : i);
}

inline PaintWorkCell& Rec(RegionContext& ctx, const CellCoord& c) {
  return (*ctx.work)[WorkIdx(ctx, c)];
}

// 0x5A00C0 GetWorkOwner：work 为空返回 -1。
inline int Owner(const RegionContext& ctx, const CellCoord& c) {
  if (ctx.work == nullptr) return -1;
  return (*ctx.work)[WorkIdx(ctx, c)].blob_id;
}

// 0x5A0090 SetWorkOwner：work 为空静默无操作。
inline void SetOwner(RegionContext& ctx, const CellCoord& c, int v) {
  if (ctx.work == nullptr) return;
  (*ctx.work)[WorkIdx(ctx, c)].blob_id = v;
}

inline bool IsOrigin(const CellCoord& c) { return c.x == 0 && c.y == 0; }

// 0x5657A0 `MapClass::GetCellAt` 的副作用部分：拿不到真实格子时把请求坐标
// 写进共享哑元的 +0x24（[0xABDC74]）。与 regions.cpp 的 Touch 同义。
inline void Touch(RegionWorld& w, const CellCoord& c) {
  if (!w.CellExists(c)) w.SetDummyCoord(c);
}

// 0x486380 未定砖（本阶段的 IsPlain）。
inline bool IsPlain(const RegionWorld& w, const CellCoord& c) {
  const int t = w.IsoTile(c);
  return t == 0 || t == 0xFFFF;
}

// 层高更新：`c->+0x11B += (char)newLevel - (char)old`（0x58D620/0x58EF10 里
// 并区/定层高那两段）。char 差先算，再按 int8 截断写回。
inline void AddLevel(RegionWorld& w, const CellCoord& c, int new_level,
                     int old) {
  const int d =
      static_cast<std::int8_t>(new_level) - static_cast<std::int8_t>(old);
  w.SetLevel(c, static_cast<std::int8_t>(w.Level(c) + d));
}

// 0x58EC9D-0x58ED38 的包围盒生长（`W == 0` 当"还没初始化"）。
inline void GrowRect(Rect* b, int x, int y) {
  if (b->w == 0) {
    *b = Rect{x, y, 1, 1};
    return;
  }
  if (x >= b->x + b->w) b->w = x - b->x + 1;
  if (x < b->x) {
    b->w += b->x - x;
    b->x = x;
  }
  if (y >= b->y + b->h) b->h = y - b->y + 1;
  if (y < b->y) {
    b->h += b->y - y;
    b->y = y;
  }
}

// 取数点 4（0x58E37D）：`do { v = _ftol(Random() × count × 1/(2³²−1)); }
// while ((uint)(count-1) < v);`——两步乘，不是 PickBelow 的折叠一次乘。
// ⚠️ count == 0 时 `(uint)(count-1) < v` 恒假，立刻接受 v=0 然后读 cands[0]
// 越界——原版行为（是否可达未确认），照抄不防护。
inline int PickCandsIndex(Randomizer& rng, std::size_t count) {
  int v;
  do {
    v = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
             static_cast<double>(count) * kInvUintMax);
  } while (static_cast<unsigned>(count - 1) < static_cast<unsigned>(v));
  return v;
}

}  // namespace

// ───────────────────────── 公共零件 ─────────────────────────

unsigned HigherMask(const RegionContext& ctx, MakingWorld& world,
                    const CellCoord& c) {
  // 0x579B70。位定义与水掩码 0x57B210 相同：bit k ⟺ AdjacentCell[(k+1)&7]。
  const int lvl = world.Level(c);          // +0x11B 按 signed char 读
  if (!ctx.grid.InMap(c)) return 0;        // 0x57A3C6 四不等式
  if (ctx.work == nullptr ||
      !(*ctx.work)[WorkIdx(ctx, c)].flag4a) {
    return 0;                              // work[c].+0x4A == 0
  }
  unsigned m = 0;
  for (int d = 0; d < 8; ++d) {
    const CellCoord n = Step(c, d);
    Touch(world, n);                       // 0x579Cxx 先取 cell 指针，界外写哑元
    if (!ctx.grid.InMap(n)) continue;      // CoordinatesLegal
    if (world.Level(n) != lvl + 4) continue;
    if (IsProtectedTerrainCell(*ctx.theater, world.IsoTile(n),
                               world.SubTile(n))) {
      continue;                            // 0x4863D0 悬崖/坡道/桥/瀑布类砖
    }
    m |= 1u << static_cast<unsigned>((d + 7) & 7);
  }
  return m;
}

std::vector<CellCoord> SplitBorderCells(const RegionContext& ctx,
                                        const RegionClass& region) {
  // 0x58E5D0。倒着走成员表，8 邻域里有一格的 owner 跟自己不同就收进来然后
  // break；越界邻格直接跳过、不算异邻。向量增长步长 max(cellCount/2, 10)。
  std::vector<CellCoord> out;
  out.reserve(static_cast<std::size_t>(std::max(region.cell_count / 2, 10)));
  for (std::size_t i = region.cells.size(); i-- > 0;) {
    const CellCoord c = region.cells[i];
    for (int d = 0; d < 8; ++d) {
      const CellCoord nb = Step(c, d);
      if (!ctx.grid.InMap(nb)) continue;
      if (Owner(ctx, nb) != region.id) {
        out.push_back(c);
        break;
      }
    }
  }
  return out;
}

float Priority(Randomizer& rng, const CellCoord& origin,
               const CellCoord& c, double base_angle) {
  // 0x58C6F0。三个权重 0.15 / 1.5 / 2.5，噪声项权重最大（每算一次吃 1 个）。
  constexpr double kHalfPi = 1.5707963267948966;   // [0x7E2820]
  constexpr double kPi = 3.141592653589793;        // [0x7E44D0]
  constexpr double kTwoPi = 6.283185307179586;     // [0x7E3CC0]
  const int dx = c.x - origin.x;
  const int dy = c.y - origin.y;
  const double d = FastSqrt(static_cast<double>(dy * dy + dx * dx));
  double a;
  if (dx == 0) {
    a = kHalfPi;
  } else {
    a = Atan(-static_cast<double>(dy) / static_cast<double>(dx));
    if (dx < 0) a += kPi;
  }
  a = std::fabs(a - base_angle);
  while (a >= kTwoPi) a -= kTwoPi;                 // 先 fabs 再模，顺序照抄
  if (a > kPi) a = kTwoPi - a;
  return static_cast<float>(
      0.15 * d + 1.5 * a +
      static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
          kInvUintMax * 2.5);
}

bool CountAtLeast(const RegionContext& ctx, const Rect& r, int id, int need) {
  // 0x5A1E50：够 need 格立刻返回 true（提前退出，不数完）。
  int hit = 0;
  for (int dy = 0; dy < r.h; ++dy) {
    for (int dx = 0; dx < r.w; ++dx) {
      const int x = r.x + dx, y = r.y + dy;
      if (!ctx.grid.InMap(x, y)) continue;
      const int owner = ctx.work == nullptr
                            ? -1
                            : (*ctx.work)[WorkIdx(ctx, CellCoord{x, y})].blob_id;
      if (owner == id && ++hit >= need) return true;
    }
  }
  return false;
}

bool Clearance(const RegionContext& ctx, MakingWorld& world, int owner,
               const Rect& r, int low_region_id) {
  // 0x594010（__thiscall，this = 高的一侧区域）。四角先过 IsWithinUsableArea
  // （use_height = 1，0x578460），再逐格查 owner 与未定砖。
  // ⚠️ 0x578460 读的是 **MapClass 实例（0x87F7E8）+0xF4/+0xFC..+0x108** 那组
  // 地图边界字段（实测 = {map_w, x=0, y=0, w=W, h=H}），**不是**传入的 rect
  // ——C++ 侧对应 ctx.usable + ctx.map_rect_w。传 r 当 local 是旧 bug
  // （整链 golden 未触发所以没暴露，ramps 专项对拍一测就现形）。
  const MapRect mr{ctx.usable.x, ctx.usable.y, ctx.usable.w, ctx.usable.h};
  const auto in_map = [&](int x, int y) {
    return IsWithinUsableArea(ctx.map_rect_w, mr, x, y, world.Level({x, y}),
                              world.Ramp({x, y}), true);
  };
  if (!in_map(r.x, r.y)) return false;                       // 左上
  if (!in_map(r.x + r.w - 1, r.y)) return false;             // 右上
  if (!in_map(r.x, r.y + r.h - 1)) return false;             // 左下
  if (!in_map(r.x + r.w - 1, r.y + r.h - 1)) return false;   // 右下
  for (int y = r.y; y < r.y + r.h; ++y) {
    for (int x = r.x; x < r.x + r.w; ++x) {
      const CellCoord c{x, y};
      const int id = Owner(ctx, c);
      if (id != owner && id != low_region_id) return false;
      if (!IsPlain(world, c)) return false;
    }
  }
  return true;
}

// ───────────────────────── 0x58EBC0 ─────────────────────────

void RebuildAndSplitRegions(RegionContext& ctx) {
  RegionSet& regions = *ctx.regions;

  // 第一遍：清空每个区域的成员表与包围盒。
  for (int i = 0; i < regions.Count(); ++i) {
    RegionClass* r = regions.At(i);
    r->cells.clear();
    r->cell_count = 0;
    r->bbox = Rect{9999, 9999, 0, 0};
  }

  // 第二遍：**倒着**扫工作数组，把格子塞回各自的区域（顺序决定切分抽种子）。
  const int side = ctx.grid.side();
  for (int i = side * side - 1; i >= 0; --i) {
    PaintWorkCell& rec = (*ctx.work)[static_cast<std::size_t>(i)];
    const CellCoord xy = rec.coord;              // +0x00 打包坐标
    if (!ctx.grid.InMap(xy)) continue;
    const int id = rec.blob_id;                  // +0x38
    if (id < 0 || id >= regions.Count()) continue;
    RegionClass* r = regions.At(id);
    r->cell_count++;
    r->cells.push_back(xy);
    GrowRect(&r->bbox, xy.x, xy.y);
  }

  // 第三遍：区域太大就切开。阈值是一次算好的一个整数：
  //   maxCells = _ftol( 2 · ((RegionSize · 0.005 + 0.05) · H) · W )
  double t = (ctx.region_size * 0.005 + 0.05) * ctx.map_h;   // ×H
  t = t * ctx.map_w;                                          // ×W
  t = t + t;                                                  // fadd st,st = ×2
  const int max_cells = Ftol(t);

restart:
  for (int i = 0; i < regions.Count(); ++i) {
    RegionClass* r = regions.At(i);
    if (r->finalized /*+0x1A*/ != 0) continue;
    if (r->wet /*+0x14*/ != 0) continue;         // 水区域不切
    if (r->cell_count > max_cells) {
      if (SplitRegion(ctx, r)) {
        regions.Destroy(r);                      // DestroyRegion + delete
        goto restart;                            // 0x58EEFC：切成功从头重扫
      }
    } else {
      r->finalized = 1;                          // 够小了，以后不再考虑
    }
  }
}

// ───────────────────────── 0x58D620 ─────────────────────────

bool SplitRegion(RegionContext& ctx, RegionClass* region) {
  RegionWorld& w = *ctx.world;
  if (region->finalized /*+0x1A*/ != 0) return false;

  const int n = region->cell_count;              // +0x0C

  // 2.1 两块临时内存：节点池（手写线性分配器）+ 一基最小堆。
  // 容量 n*2+10：扩散最多弹 take ≤ n/3 次、每次最多压 4 个 ⇒
  // 节点数 ≤ 1 + 4n/3 < 2n + 10。压堆前还有 `count+1 < cap` 的门，满则静默丢弃。
  const int cap = n * 2 + 10;
  NodePool pool(cap);
  MinHeap heap(&pool, cap);

  // 2.2 标记协议：+0x3C 全图清 -1；本区格子 +0x38 临时写 -2（待定）。
  for (PaintWorkCell& rec : *ctx.work) rec.queued = -1;
  for (const CellCoord& c : region->cells) Rec(ctx, c).blob_id = -2;

  // 2.3 取数点 1（0x58D782）：take = PickRange(n/8, n/3)。内联拒绝采样，
  // 两步乘 + 加 lo，拒绝 v > hi——与 PickRange 语义一致。
  const int lo = n / 8;
  const int hi = n / 3;
  const int take = PickRange(*ctx.rng, lo, hi);

  // 2.4 取数点 2（0x58D7CD）：种子格 = cells[PickBelow(cells.count)]。
  // 注意抽的是向量 count，与 +0x0C 的 cellCount 未必相等（扩容失败时不涨）。
  int seed_idx;
  const std::size_t cell_count = region->cells.size();
  do {
    seed_idx = Ftol(static_cast<double>(static_cast<std::uint32_t>(
                        ctx.rng->Random())) *
                    static_cast<double>(cell_count) * kInvUintMax);
  } while (static_cast<unsigned>(seed_idx) >
           static_cast<unsigned>(cell_count - 1));
  const CellCoord seed = region->cells[static_cast<std::size_t>(seed_idx)];
  Rec(ctx, seed).queued = -3;
  pool.Alloc(seed.x, seed.y, 0.0f);
  heap.Push(0);
  int next = 1;

  // 2.5 取数点 3（0x58D8BD）：基准角 = Random() × 2π/(2³²−1)。
  constexpr double kTwoPiInvUintMax = 1.4629180796077717e-09;  // [0x7ED8A8]
  constexpr double kPiOver8 = 0.39269908169872414;             // [0x7ED8A0] π/8
  double angle =
      static_cast<double>(static_cast<std::uint32_t>(ctx.rng->Random())) *
      kTwoPiInvUintMax;

  // 2.6 优先级扩散：只走四个正方向（d += 2），基准角每弹一格随机游走一次。
  int taken = 0;
  int cur = heap.Pop();
  while (cur >= 0 && taken < take) {
    const PaintNode& node = pool[cur];
    Rec(ctx, CellCoord{node.x, node.y}).blob_id = -3;   // 确定剥走

    for (int d = 0; d < 8; d += 2) {
      const CellCoord nb = Step(CellCoord{node.x, node.y}, d);
      if (!ctx.grid.InMap(nb)) continue;
      PaintWorkCell& rec = Rec(ctx, nb);
      if (rec.blob_id != -2) continue;                 // 不是本区待定格
      // ⚠️ 无防重：原版 0x89A304 方向偏移表是 .data 零填充区（全 0），
      // 候选标记写的是"节点自己"（0x58DA5B），候选格从不被标 +0x3C=-3，
      // 于是同一个候选能被多个邻格重复发现、重复入堆。C++ 照抄不防重。
      if (!IsPlain(w, nb)) continue;                   // 0x486380
      const float key = Priority(*ctx.rng, seed, nb, angle);  // 每格 1 随机数
      const int slot = pool.Alloc(nb.x, nb.y, key);
      if (slot >= 0) heap.Push(slot);                  // 满则静默丢弃（照抄）
      ++next;
    }

    ++taken;
    angle += (*ctx.normal)() * kPiOver8;               // 每弹一格 2 随机数
    cur = heap.Pop();
  }

  // 2.7 尾巴：+0x3C 还原 -1；堆里没弹出来的那一圈候选格也一起剥走
  // （0x58D620 行 252-276：主循环后先弹一次，再 while 把剩余堆全标 -3）。
  for (PaintWorkCell& rec : *ctx.work) rec.queued = -1;
  while ((cur = heap.Pop()) >= 0) {
    const PaintNode& node = pool[cur];
    Rec(ctx, CellCoord{node.x, node.y}).blob_id = -3;
  }

  // 2.8 连通块重建：倒着遍历本区成员表，凡是 +0x38 < -1 的格子（-2 留下的、
  // -3 剥走的都算）就起新区域，然后**八方向**栈式洪泛收连通块。
  // ⚠️ 扩散用四方向、重建用八方向——两处不一致，照抄。
  std::vector<RegionClass*> fresh;
  for (std::size_t i = region->cells.size(); i-- > 0;) {
    const CellCoord c = region->cells[i];
    const int mark = Rec(ctx, c).blob_id;
    if (mark >= -1) continue;                          // 已被前面的洪泛认领
    const int seed_level = w.Level(c);
    const bool wet = IsWaterOrShoreTile(w.IsoTile(c), *ctx.theater);
    RegionClass* r = ctx.regions->Create(c, seed_level, wet);
    r->cells.push_back(c);
    Rec(ctx, c).blob_id = r->id;
    ++r->cell_count;                   // 0x58D620：种子格也数进 +0x0C
    fresh.push_back(r);

    std::vector<CellCoord> stack;
    stack.push_back(c);
    while (!stack.empty()) {
      const CellCoord p = stack.back();                // 先减再取 == 取末位再删
      stack.pop_back();
      for (int d = 0; d < 8; ++d) {
        const CellCoord nb = Step(p, d);
        if (!ctx.grid.InMap(nb)) continue;
        if (Rec(ctx, nb).blob_id != mark) continue;
        r->cells.push_back(nb);
        Rec(ctx, nb).blob_id = r->id;
        ++r->cell_count;               // 0x58D620：每个成员格 +0x0C++
        stack.push_back(nb);
      }
    }
  }

  // 2.9 给每个新区域定层高（倒序，跳过已作废的）。
  for (std::size_t fi = fresh.size(); fi-- > 0;) {
    RegionClass* r = fresh[fi];
    if (r->discarded /*+0x24*/ != 0) continue;

    // 先取边界格（0x58E5D0），再统计 8 邻域里出现过哪些区域。
    const std::vector<CellCoord> border = SplitBorderCells(ctx, *r);
    std::vector<char> seen(static_cast<std::size_t>(ctx.regions->next_id()), 0);
    for (const CellCoord& c : border) {
      for (int d = 0; d < 8; ++d) {
        const CellCoord nb = Step(c, d);
        if (!ctx.grid.InMap(nb)) continue;
        const int id = Rec(ctx, nb).blob_id;
        if (id >= 0 && id != r->id) seen[static_cast<std::size_t>(id)] = 1;
      }
    }

    RegionClass* best = nullptr;
    int best_cells = -1;
    int best_level = -1;
    int min_l = -1, max_l = -1;
    for (int id = 0; id < ctx.regions->next_id(); ++id) {
      if (!seen[static_cast<std::size_t>(id)]) continue;
      RegionClass* q = ctx.regions->ById(id);
      if (q->discarded /*+0x24*/) continue;            // ⚠️ q 为 nullptr 直接崩（原版）
      if (q->cell_count > best_cells && !q->wet) {
        best = q;
        best_cells = q->cell_count;
        best_level = q->level;
      }
      if (min_l == -1) {
        min_l = max_l = q->level;
      } else {
        max_l = std::max(max_l, q->level);
        min_l = std::min(min_l, q->level);
      }
    }

    // 候选层高表：层高只有 0/4/8 三档。
    std::vector<int> cands;
    if (max_l == min_l) {
      if (min_l > 3) cands.push_back(min_l - 4);
      if (max_l < 8) cands.push_back(max_l + 4);
    } else if (max_l - min_l == 4) {
      cands.push_back(max_l);
      cands.push_back(min_l);
    } else if (max_l - min_l == 8) {
      cands.push_back((max_l + min_l) / 2);
    }
    if (ctx.map_type == 3 || ctx.map_type == 4) {      // 水图不许压到 0 层
      for (std::size_t i = cands.size(); i-- > 0;) {
        if (cands[i] == 0) cands.erase(cands.begin() + static_cast<long>(i));
      }
    }

    // 2.10 小块（<101 格）并进最大的邻区；否则取数点 4 抽新层高。
    if (r->cell_count < 0x65) {
      if (best) {
        const int old = r->level;
        r->level = best_level;
        const int d =
            static_cast<std::int8_t>(best_level) - static_cast<std::int8_t>(old);
        ctx.grid.ForEachCell([&](const CellCoord& c) {
          if (Owner(ctx, c) == r->id) w.SetLevel(c, w.Level(c) + d);
        });
        for (std::size_t i = r->cells.size(); i-- > 0;) {
          best->cells.push_back(r->cells[i]);
          Rec(ctx, r->cells[i]).blob_id = best->id;
          ++best->cell_count;          // 0x58D620：并入的每格 +0x0C++
        }
        r->discarded = 1;
      }
      // best == nullptr：什么都不做，这个小区域原样留着
    } else {
      const int old = r->level;
      const int v = PickCandsIndex(*ctx.rng, cands.size());
      r->level = cands[static_cast<std::size_t>(v)];   // ⚠️ cands 空时越界（原版）
      const int d = static_cast<std::int8_t>(r->level) - static_cast<std::int8_t>(old);
      ctx.grid.ForEachCell([&](const CellCoord& c) {
        if (Owner(ctx, c) == r->id) w.SetLevel(c, w.Level(c) + d);
      });
    }
  }

  // 2.11 收尾：倒着扫全局区域表，把自己以外已作废的全摘掉。
  for (int i = ctx.regions->Count() - 1; i >= 0; --i) {
    RegionClass* q = ctx.regions->At(i);
    if (q != region && q->discarded && q != nullptr) {
      ctx.regions->Destroy(q);
    }
  }
  region->split_count /*+0x1C*/ += 1;
  return true;
}

// ───────────────────────── 0x58EF10 ─────────────────────────

void RebuildRegions(RegionContext& ctx, MakingWorld& world) {
  // 原版开头还有 SetActiveFoundation(0)（DisplayClass 视野，与地形无关）。
  bool ok = true;
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    if (!ok) return;
    ok = FillNotch(ctx, world, c, -1);                 // FUN_00579010(c, -1)
  });

  const int side = ctx.grid.side();
  for (int i = 0; i < side * side; ++i) {
    PaintWorkCell& rec = (*ctx.work)[static_cast<std::size_t>(i)];
    rec.blob_id = -1;
    rec.queued = -1;
  }
  for (int i = ctx.regions->Count() - 1; i >= 0; --i) {
    ctx.regions->Destroy(ctx.regions->At(i));
  }
  ctx.regions->set_next_id(0);                         // [0xABED14] = 0

  // 按连通块重建区域（与阶段 3 的 CreateRegions 同构，(0,0) 哨兵照跳）。
  for (int i = 0; i < side * side; ++i) {
    PaintWorkCell& rec = (*ctx.work)[static_cast<std::size_t>(i)];
    if (rec.blob_id != -1) continue;
    if (IsOrigin(rec.coord)) continue;                 // 0x5AC370 不等比较
    RegionClass* r = SeedRegion(ctx, rec.coord);       // 0x58C800，含取数点
    if (r) r->finalized /*+0x1A*/ = 0;
  }

  if (ok) {
    for (int i = 0; i < ctx.regions->Count(); ++i) {
      BuildNeighbourList(ctx, ctx.regions->At(i));     // 0x58F0C0
    }
    for (int i = 0; i < ctx.regions->Count(); ++i) {
      MakeConnections(ctx, world, ctx.regions->At(i)); // 0x5905D0
    }
    for (int i = 0; i < ctx.regions->Count(); ++i) {
      ctx.regions->At(i)->neighbours.clear();          // delete + 置 0
    }
  }
}

// ───────────────────────── 0x58F0C0 ─────────────────────────

void BuildNeighbourList(RegionContext& ctx, RegionClass* r) {
  r->neighbours.clear();                               // new(10) 的等价物
  std::vector<char> seen(static_cast<std::size_t>(ctx.regions->next_id()), 0);

  // ⚠️ 用 0x58D410 那版边界格（regions.h 的 GetBorderCells），不是 0x58E5D0。
  const std::vector<CellCoord> border = GetBorderCells(ctx, *r);
  for (const CellCoord& b : border) {
    for (int d = 0; d < 8; ++d) {
      const CellCoord nb = Step(b, d);
      if (!ctx.grid.InMap(nb)) continue;
      const int id = Rec(ctx, nb).blob_id;
      if (id >= 0) seen[static_cast<std::size_t>(id)] = 1;
    }
  }

  for (int id = 0; id < ctx.regions->next_id(); ++id) {
    if (seen[static_cast<std::size_t>(id)] && id != r->id) {
      r->neighbours.push_back(id);
    }
  }

  // 顺手重数一遍自己的格子（这次连 (0,0) 哨兵一起排除）。
  r->cell_count = 0;
  const int side = ctx.grid.side();
  for (int i = 0; i < side * side; ++i) {
    const PaintWorkCell& rec = (*ctx.work)[static_cast<std::size_t>(i)];
    if (rec.blob_id == r->id && !IsOrigin(rec.coord)) r->cell_count++;
  }
}

// ───────────────────────── 0x5905D0 ─────────────────────────

void MakeConnections(RegionContext& ctx, MakingWorld& world, RegionClass* r) {
  const MapRect usable{ctx.usable.x, ctx.usable.y, ctx.usable.w, ctx.usable.h};
  const auto in_usable = [&](const CellCoord& c) {
    return IsWithinUsableArea(ctx.map_rect_w, usable, c.x, c.y,
                              world.Level(c), world.Ramp(c), true);
  };

  if (r->wet) {
    // 水域区域：让隔水相望的两个陆地邻区连起来（架桥）。
    if (r->neighbours.size() < 2) return;              // 写成 count != 1 && ...
    for (std::size_t i = 1; i < r->neighbours.size(); ++i) {
      RegionClass* a = ctx.regions->ById(r->neighbours[i - 1]);
      if (!(a->neighbours.size() > 1 || a->cell_count > 0x32)) continue;
      for (std::size_t j = i; j < r->neighbours.size(); ++j) {  // 每轮重读
        RegionClass* b = ctx.regions->ById(r->neighbours[j]);
        if (!(b->neighbours.size() > 1 || b->cell_count > 0x32)) continue;
        if (b->wet) continue;
        if (b->level != a->level || a->level != r->level) continue;
        BuildBridge(ctx, world, r, a, b);              // 5 个取数点
      }
    }
    return;
  }

  // 陆地区域：给每一对高度不同的相邻区域开坡道。
  for (std::size_t k = 0; k < r->neighbours.size(); ++k) {
    RegionClass* nb = ctx.regions->ById(r->neighbours[k]);
    if (nb->id <= r->id) continue;                     // 每对只处理一次
    if (nb->level == r->level) continue;               // 同层不用坡道

    // 取数点 0x590792：无条件抽 0..100。
    const int roll = PickBelow(*ctx.rng, 101);
    // 取数点 0x5907C0：命中 Accessibility 才再抽一次 0..2。
    const int extra = (roll < ctx.accessibility) ? PickBelow(*ctx.rng, 3) : 0;
    const int want = extra + 1;                        // 要开 1~3 条

    RegionClass* hi = (r->level < nb->level) ? nb : r; // 层高大的那个
    const int lo = (r->level < nb->level) ? r->id : nb->id;
    const std::vector<CellCoord> border = GetBorderCells(ctx, *hi);  // 0x58D410

    int made = 0;
    for (int tries = 0; made < want; ++tries) {        // 注意是 do-while 变体
      if (tries > 99) break;                           // 100 次上限
      // 取数点 0x590889：抽边界格。border 为空时 count-1 下溢，直接越界读（原版）。
      const int idx = PickBelow(*ctx.rng, static_cast<int>(border.size()));
      const CellCoord c = border[static_cast<std::size_t>(idx)];
      if (in_usable(c)) {
        made += BuildRamp(ctx, world, hi, c, lo,
                          static_cast<float>(tries) * 0.01f)
                    ? 1
                    : 0;
      }
    }
    if (made == 0) r->connected /*+0x1B*/ = 0;
  }
}

// ───────────────────────── 调度 ─────────────────────────

void MakeRegions(RegionContext& ctx, MakingWorld& world, int map_type) {
  if (map_type != 3 && map_type != 4) return;
  ctx.map_type = map_type;
  RebuildAndSplitRegions(ctx);        // 0x58EBC0
  RebuildRegions(ctx, world);         // 0x58EF10
  TrimCliffCorners(ctx, world);       // 0x5A19E0
  RepaintAll(ctx, world);             // 0x578E60
  RerollMultitileCliffs(ctx, world);  // 0x5A17F0
}

}  // namespace rmg
}  // namespace ra2
