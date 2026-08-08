#include "regions.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "blob_heap.h"        // 与涂块核心共用的一基最小堆（下沉 0x5AD870）
#include "place_tile.h"      // IsUndecidedTile（0x486380）
#include "scaled_random.h"   // PickBelowMul（0x58CAF9 的内联展开）

namespace ra2 {
namespace rmg {

namespace {

// 0x89F688，运行期由 0x49F2F0 填出：0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW。
// 这里单独放一份，免得为了一张 8 项的表把 cell_ref.h 整条依赖链拖进来。
const int kDeltaX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int kDeltaY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// 0x42D510 `CellStruct::operator+`：两个分量都是 **int16** 相加后再符号扩展。
// 地图不超过 512 格时溢出不会发生，但宽度照抄，不替原版"修正"。
inline CellCoord Step(const CellCoord& c, int dir) {
  return CellCoord{static_cast<std::int16_t>(c.x + kDeltaX[dir]),
                   static_cast<std::int16_t>(c.y + kDeltaY[dir])};
}

// 0x5657A0 `MapClass::GetCellAt` 的**副作用**部分：拿不到真实格子时把请求坐标
// 写进共享哑元的 +0x24（[0xABDC74]）。取到之后的字段读写由各访问器自行落到
// 哑元上（见 RegionWorld 的注释），所以这里只需要"碰一下"。
inline void Touch(RegionWorld& w, const CellCoord& c) {
  if (!w.CellExists(c)) w.SetDummyCoord(c);
}

inline std::size_t WorkIdx(const RegionContext& ctx, const CellCoord& c) {
  const long long i =
      static_cast<long long>(c.y) * ctx.grid.side() + c.x;
  // 原版是裸算下标、不查界。所有调用点都先过了菱形判定或是线性扫描，
  // 所以这里只做一层防御性夹取，正常路径永远不会触发。
  if (i < 0) return 0;
  const long long n = static_cast<long long>(ctx.work->size());
  return static_cast<std::size_t>(i >= n ? n - 1 : i);
}

inline PaintWorkCell& Rec(RegionContext& ctx, const CellCoord& c) {
  return (*ctx.work)[WorkIdx(ctx, c)];
}

// 0x5A00C0 `GetWorkOwner`（__stdcall）：工作数组为空指针时返回 -1。
inline int Owner(const RegionContext& ctx, const CellCoord& c) {
  if (ctx.work == nullptr) return -1;
  return (*ctx.work)[WorkIdx(ctx, c)].blob_id;
}

// 0x5A0090 `SetWorkOwner`（__stdcall）：工作数组为空指针时静默无操作。
inline void SetOwner(RegionContext& ctx, const CellCoord& c, int v) {
  if (ctx.work == nullptr) return;
  (*ctx.work)[WorkIdx(ctx, c)].blob_id = v;
}

inline bool IsOrigin(const CellCoord& c) { return c.x == 0 && c.y == 0; }

// 「这一格是水岸或绿地」——CreateRegions 起种的谓词，也是 RegionClass +0x14
// 的生效值。两个子判据的顺序照 0x58C86B 起的短路顺序。
inline bool Wetness(RegionWorld& w, const TheaterData& t, const CellCoord& c) {
  const int tile = w.IsoTile(c);
  return IsWaterOrShoreTile(tile, t) || IsGreenTile(tile, t);
}

}  // namespace

// ────────────────────────────── 砖号谓词 ──────────────────────────────

bool IsWaterOrShoreTile(int tile, const TheaterData& t) {
  // 0x4865D0。岸砖那一支是提前 return，其余五个区间是一个大合取的反面；
  // 对纯 OR 而言顺序无所谓，但一条 -1 保护都没有这件事有所谓。
  if (tile >= t.shore_pieces && tile < t.shore_pieces + 0x2A) return true;
  if (tile >= t.water_set && tile < t.water_set + 0x0E) return true;
  if (tile >= t.waterfall_east && tile < t.waterfall_east + 4) return true;
  if (tile >= t.waterfall_west && tile < t.waterfall_west + 4) return true;
  if (tile >= t.waterfall_south && tile < t.waterfall_south + 4) return true;
  if (tile >= t.waterfall_north && tile < t.waterfall_north + 4) return true;
  return false;
}

bool IsGreenTile(int tile, const TheaterData& t) {
  // 0x4867B0
  if (tile == t.green_tile) return true;
  return tile >= t.clear_to_green_lat && tile < t.clear_to_green_lat + 0x10;
}

// ────────────────────────────── RegionSet ──────────────────────────────

RegionClass* RegionSet::Create(const CellCoord& seed, int level, bool wet) {
  // 0x58BF70。ctor 自己会按种子格算 +0x10/+0x14，但调用方 0x58CE21/0x58CE24
  // 立刻覆盖，所以这里直接收生效值。
  pool_.push_back(std::unique_ptr<RegionClass>(new RegionClass()));
  RegionClass* r = pool_.back().get();
  r->id = next_id_;          // +0x08 = [0xABED14]
  r->cell_count = 0;         // +0x0C
  r->level = level;          // +0x10
  r->wet = wet;              // +0x14
  r->seed = seed;            // +0x16
  r->finalized = false;      // +0x1A = 0
  r->connected = true;       // +0x1B = 1
  r->split_count = 0;        // +0x1C
  r->start_points = 0;       // +0x20
  r->discarded = false;      // +0x24
  ++next_id_;                // 0x58BFE9 `inc [0xABED14]`
  list_.push_back(r);        // 登记进 [0xABDF94] / [0xABDFA0]
  return r;
}

void RegionSet::Destroy(RegionClass* r) {
  // 0x5AC290(deallocate=1)：析构 +0x00 候选向量（**不碰 +0x04**）、
  // 把自己从全局表里摘掉（后面元素前移、个数 -1）、释放 +0x2C、free(this)。
  r->candidates.clear();
  const auto it = std::find(list_.begin(), list_.end(), r);
  if (it != list_.end()) list_.erase(it);
  r->cells.clear();
  for (auto p = pool_.begin(); p != pool_.end(); ++p) {
    if (p->get() == r) {
      pool_.erase(p);
      return;
    }
  }
}

void RegionSet::Reset() {
  // 0x598986-0x5989A0：i 从 count-1 递减到 0，非空就销毁；随后 [0xABED14] = 0。
  for (int i = Count() - 1; i >= 0; --i) Destroy(At(i));
  next_id_ = 0;
}

RegionClass* RegionSet::ById(int id) {
  for (RegionClass* r : list_) {
    if (r->id == id) return r;
  }
  return nullptr;   // ⚠️ 原版调用方直接解引用，不查空（未确认）
}

// ──────────────────────────── 两个边界格函数 ────────────────────────────

std::vector<CellCoord> CollectBorderCells(const RegionContext& ctx, int owner) {
  std::vector<CellCoord> out;
  // 0x5A0714：候选向量的增长步长 = [MapSeed+0x184] * [MapSeed+0x180] = H * W。
  // DynamicVectorClass 在步长 <= 0 时 push 静默失败，而初始容量是 0——
  // 于是 W 或 H 为 0 时本函数恒返回空表。
  const long long cap_step =
      static_cast<long long>(ctx.map_h) * static_cast<long long>(ctx.map_w);
  const long long n =
      static_cast<long long>(ctx.grid.side()) * ctx.grid.side();
  if (n < 1) return out;      // 0x5A0729 `if (local_4 < 1) return`
  for (long long i = 0; i < n; ++i) {
    // ⚠️ 坐标取的是记录**自己存的 +0x00**，再用它反算工作数组下标；
    // 不是拿循环下标去反推坐标。
    const CellCoord c = (*ctx.work)[static_cast<std::size_t>(i)].coord;
    if (IsOrigin(c)) continue;
    if (Owner(ctx, c) != owner) continue;
    for (int d = 0; d < 8; ++d) {
      const CellCoord m = Step(c, d);
      if (!ctx.grid.InMap(m)) continue;   // 界外**不算**"归属不同"
      if (Owner(ctx, m) == owner) continue;
      if (cap_step > 0) out.push_back(c);
      break;                              // 一格只入表一次
    }
  }
  return out;
}

std::vector<CellCoord> GetBorderCells(RegionContext& ctx,
                                      const RegionClass& region) {
  RegionWorld& w = *ctx.world;
  std::vector<CellCoord> out;
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    if (Owner(ctx, c) != region.id) return;
    bool any = false;
    for (int d = 0; d < 8; ++d) {
      const CellCoord m = Step(c, d);
      Touch(w, m);              // 0x481810 先查表再判界，界外会写哑元坐标
      if (!ctx.grid.InMap(m)) continue;
      if (Owner(ctx, m) != region.id) any = true;   // 不 break，走满 8 次
    }
    if (any) out.push_back(c);
  });
  return out;
}

// ─────────────────────────────── SeedRegion ───────────────────────────────

namespace {

// LAB_0058CDF6：真的建一个区域，并数出成员格数。
RegionClass* BuildRegion(RegionContext& ctx, const CellCoord& seed, int level,
                         bool wet) {
  RegionWorld& w = *ctx.world;
  // ctor 自己会取两次种子格（算它那份马上被覆盖的 +0x10/+0x14），
  // 两次取格的哑元副作用照留。
  Touch(w, seed);
  Touch(w, seed);
  RegionClass* r = ctx.regions->Create(seed, level, wet);
  // 0x58CE2E-0x58CE71：线性扫工作数组，归属等于本区域且打包坐标非 (0,0) 就 +1。
  // 「非 (0,0)」这一条是 0x5AC370(this = &rec.coord, arg = {0,0}) 的返回值。
  const long long n =
      static_cast<long long>(ctx.grid.side()) * ctx.grid.side();
  for (long long i = 0; i < n; ++i) {
    const PaintWorkCell& rec = (*ctx.work)[static_cast<std::size_t>(i)];
    if (rec.blob_id != r->id) continue;
    if (IsOrigin(rec.coord)) continue;
    ++r->cell_count;
  }
  return r;
}

// 把整张图上归属为 `from` 的格子改判成 `to`，并把砖与层高重置。
// 两条并区分支共用这一段（0x58CC10 起与 0x58CD3E 起，逐条同构）。
void Reassign(RegionContext& ctx, int from, int to, int level_src) {
  RegionWorld& w = *ctx.world;
  const int lvl = (level_src != -1) ? level_src : ctx.fallback_level;
  ctx.grid.ForEachCell([&](const CellCoord& p) {
    PaintWorkCell& rec = Rec(ctx, p);
    if (rec.blob_id != from) return;
    rec.blob_id = to;
    rec.flag4b = false;        // +0x4B = 0
    w.SetIsoTile(p, 0);        // +0x38 = 0
    w.SetSubTile(p, 0);        // +0x11A = 0
    w.SetLevel(p, lvl);        // +0x11B
  });
}

}  // namespace

RegionClass* SeedRegion(RegionContext& ctx, const CellCoord& seed) {
  RegionWorld& w = *ctx.world;
  const TheaterData& t = *ctx.theater;

  // 0x58C80D：本次要用的编号先取下来。走并区分支时它**不会**被消耗，
  // 下一次起种还是同一个号。
  const int id = ctx.regions->next_id();

  std::vector<CellCoord> stack;     // 栈上 DynamicVector，增长步长 50000
  stack.push_back(seed);

  const bool wet = Wetness(w, t, seed);
  const int level = w.Level(seed);
  int visited = 0;

  // ── DFS（后进先出）涂一块同层同"湿度"的连通区 ──
  while (!stack.empty()) {
    const CellCoord cur = stack.back();
    stack.pop_back();
    // +0x3C 是内联直写（**没有**空指针保护），+0x38 走 0x5A0090（有保护）。
    Rec(ctx, cur).queued = id;
    SetOwner(ctx, cur, id);
    for (int d = 0; d < 8; ++d) {
      const CellCoord n = Step(cur, d);
      if (!ctx.grid.InMap(n)) continue;
      PaintWorkCell& rec = Rec(ctx, n);
      if (rec.blob_id != -1) continue;    // 已归属
      if (rec.queued == id) continue;     // 已进过队列
      rec.queued = id;
      Touch(w, n);
      if (w.Level(n) != level) continue;  // 只走同层
      Touch(w, n);
      if (Wetness(w, t, n) != wet) continue;
      stack.push_back(n);
    }
    ++visited;
  }

  if (visited > 0x4A) return BuildRegion(ctx, seed, level, wet);   // 75 格及以上

  // ── 不足 75 格：按三条互斥规则处理 ──
  if (wet) return BuildRegion(ctx, seed, level, wet);   // 水/绿的小块照样建

  if (id == 0) {
    // 没有"前一个区域"可并 —— 全生成器唯一在这儿抽随机数（0x58CAF9）。
    std::vector<CellCoord> borders = CollectBorderCells(ctx, 0);
    const int n = static_cast<int>(borders.size());
    // PickRange(0, n-1) 的内联展开，与 0x598030 的乘法顺序完全一致。
    const int pick = PickBelowMul(*ctx.rng, n);
    // ⚠️ n == 0 时 hi = -1，无符号比较恒真，第一次抽就被接受，随后原版按
    // pick 下标去读一个**空数组**（越界读）。这里退化成 (0,0)；随机数照抽。
    // 未确认：这条路在真机上读到什么。
    const CellCoord chosen =
        (pick >= 0 && pick < n) ? borders[static_cast<std::size_t>(pick)]
                                : CellCoord{0, 0};
    for (int d = 0; d < 8; ++d) {
      const CellCoord m = Step(chosen, d);
      if (!ctx.grid.InMap(m)) continue;
      Touch(w, m);
      Touch(w, chosen);
      if (w.Level(m) == w.Level(chosen)) continue;   // 要找层高**不同**的邻格
      Touch(w, m);
      if (IsWaterOrShoreTile(w.IsoTile(m), t)) continue;   // 且不能是水岸
      Touch(w, m);
      // 把整块 0 号退回"未归属"，让它之后被重新起种。
      //
      // ⚠️ 这里只把 +0x38 刷回 -1，**不动 +0x3C**，而 +0x3C 此时正好是
      // 0 == id。于是下一次起种（编号没被消耗，还是 id == 0）踩到这些格子时
      // 会被 "queued == id" 那条当成"已入过队列"直接跳过——DFS 再也长不出去。
      // 实测（regions_golden.txt 的 syn-dry-* 共 631 个取数点）：除了头几个，
      // 其余 611 个的边界格表长度都是 **1**——整条循环退化成"逐格重置"。
      // 这是 25 年前的真实行为，照拄，不修。
      Reassign(ctx, /*from=*/0, /*to=*/-1, w.Level(m));
      return nullptr;
    }
    // 8 个方向全试过都没成 → 落回"建区域"
    return BuildRegion(ctx, seed, level, wet);
  }

  // id != 0：往西找一格，不在界内就往北。
  CellCoord m{static_cast<std::int16_t>(seed.x - 1), seed.y};
  if (!ctx.grid.InMap(m)) {
    m = CellCoord{seed.x, static_cast<std::int16_t>(seed.y - 1)};
  }
  if (!ctx.grid.InMap(m)) return BuildRegion(ctx, seed, level, wet);

  const int new_owner = Owner(ctx, m);
  Touch(w, m);
  Reassign(ctx, /*from=*/id, /*to=*/new_owner, w.Level(m));
  return nullptr;
}

// ───────────────────────── CreateRegions / Grow / 收尾 ─────────────────────────

void CreateRegions(RegionContext& ctx) {
  RegionWorld& w = *ctx.world;
  const TheaterData& t = *ctx.theater;
  const long long n =
      static_cast<long long>(ctx.grid.side()) * ctx.grid.side();
  for (long long i = 0; i < n; ++i) {
    const CellCoord c = (*ctx.work)[static_cast<std::size_t>(i)].coord;
    // ⚠️ 取格在三条判定**之前**、且无条件执行（0x58CFAE）。菱形外的槽位
    // 坐标是 (0,0)，那一格不存在，于是每扫到一条就往哑元写一次 (0,0)。
    // 这是能被后续读到的状态，不能挪到 if 里面去。
    Touch(w, c);
    if ((*ctx.work)[static_cast<std::size_t>(i)].blob_id != -1) continue;
    if (IsOrigin(c)) continue;
    if (!Wetness(w, t, c)) continue;
    SeedRegion(ctx, c);
  }
}

bool Grow(RegionContext& ctx, RegionClass& region, int rings) {
  RegionWorld& w = *ctx.world;
  const TheaterData& t = *ctx.theater;
  std::vector<CellCoord> cur = GetBorderCells(ctx, region);
  for (int it = 0; it < rings; ++it) {
    std::vector<CellCoord> next;
    // 0x58E77B：下一圈向量的增长步长 = 当前圈格数 * 3（步长 <= 0 时 push 失败）。
    const long long cap_step = static_cast<long long>(cur.size()) * 3;
    for (std::size_t i = 0; i < cur.size(); ++i) {
      for (int d = 0; d < 8; ++d) {
        const CellCoord n = Step(cur[i], d);
        if (!ctx.grid.InMap(n)) continue;
        Touch(w, n);
        if (w.Level(n) != region.level) continue;   // 只吃同层
        PaintWorkCell& rec = Rec(ctx, n);
        if (rec.blob_id == -1) {
          Touch(w, n);
          bool ok = IsUndecidedTile(w.IsoTile(n));
          if (!ok) {
            Touch(w, n);
            ok = IsGreenTile(w.IsoTile(n), t);
          }
          if (ok) {
            if (cap_step > 0) next.push_back(n);
            rec.blob_id = region.id;
            Touch(w, n);
            w.SetLevel(n, region.level);
            continue;
          }
          // ⚠️ 谓词不过 → 落到下面那条 `!= region.id` 判定，而此时
          // rec.blob_id 仍是 -1，于是必然成立 → 整个 Grow 立刻放弃。
        }
        if (rec.blob_id != region.id) return false;
      }
    }
    cur = std::move(next);
  }
  return true;
}

void FloodRest(RegionContext& ctx, RegionClass& region) {
  std::vector<CellCoord> stack = GetBorderCells(ctx, region);
  while (!stack.empty()) {
    const CellCoord cur = stack.back();
    stack.pop_back();
    for (int d = 0; d < 8; ++d) {
      const CellCoord n = Step(cur, d);
      if (!ctx.grid.InMap(n)) continue;
      PaintWorkCell& rec = Rec(ctx, n);
      if (rec.blob_id != -1) continue;
      if (!rec.flag4b) continue;      // +0x4B 必须非 0
      rec.blob_id = region.id;        // ⚠️ 归属先写，再尝试入栈
      stack.push_back(n);
    }
  }
  // 不改砖、不改层高——与 Grow 的区别就在这儿。
}

void Finalize(RegionContext& ctx, bool flag) {
  RegionWorld& w = *ctx.world;
  const long long n =
      static_cast<long long>(ctx.grid.side()) * ctx.grid.side();
  for (long long i = 0; i < n; ++i) {
    const CellCoord c = (*ctx.work)[static_cast<std::size_t>(i)].coord;
    // 这里的取格在 if **里面**（0x58D03A），与 CreateRegions 相反。
    if ((*ctx.work)[static_cast<std::size_t>(i)].blob_id != -1) continue;
    if (IsOrigin(c)) continue;
    Touch(w, c);
    RegionClass* r = SeedRegion(ctx, c);     // 不经任何谓词过滤
    if (r != nullptr) r->finalized = flag;
  }
}

void InitRegions(RegionContext& ctx, int map_type) {
  // 1) 工作记录的两个区域字段清 -1（**整个方阵**，不止菱形内），再拆掉旧区域。
  if (ctx.work != nullptr) {
    for (PaintWorkCell& rec : *ctx.work) {
      rec.blob_id = -1;
      rec.queued = -1;
    }
  }
  ctx.regions->Reset();

  // 2) 按连通块建区域
  CreateRegions(ctx);

  // 3) 每个 wet 区域先长几圈再漫灌。
  // ⚠️ 循环上界是**每轮重读**的区域个数（0x598CF0 `mov eax,[0xABDFA0]`）。
  // 这一段里没人建/删区域，所以实际等价于固定上界，但照抄写法。
  for (int i = 0; i < ctx.regions->Count(); ++i) {
    RegionClass* r = ctx.regions->At(i);
    if (!r->wet) continue;
    const bool big_map = (map_type == 3 || map_type == 4);
    const int rings = (r->cell_count > 8000 ? 1 : 0) + (big_map ? 0 : 1) + 4;
    Grow(ctx, *r, rings);      // 返回值被调度方**忽略**（0x598CE4 之后没有 test）
    FloodRest(ctx, *r);
  }

  // 4) 剩下没归属的格子再兜一遍；0x598CFA `xor cl,cl` → flag = 0
  Finalize(ctx, false);
}


// ── 0x5A0410 Erode ──────────────────────────────────────────────────────
//
// 栈帧（call 0x5A0700 返回后 esp 记作 E）：
//   [E+0x34] owner   [E+0x38] rings   [E+0x3C] newOwner   [E+0x2C] this
//   [E+0x10] 当前圈的边界向量   [E+0x14] ring   [E+0x18]/[E+0x1C] 打包坐标
bool Erode(RegionContext& ctx, int level_30c, int owner, int rings,
           int new_owner) {
  // 0x5A0422：第一圈就是 owner 的全部边界格
  std::vector<CellCoord> cur = CollectBorderCells(ctx, owner);

  // 0x5A042D `cmp esi,1 / jle`：只剥一圈时**不清**戳，戳里残留的是别处写的值
  if (rings > 1) {
    ctx.grid.ForEachCell([&](const CellCoord& c) { Rec(ctx, c).queued = 0; });
  }

  for (int ring = 0; ring < rings; ++ring) {   // 0x5A0489 起
    // 0x5A0495-0x5A04F4：倒着走当前边界向量，逐格剥掉
    for (int j = static_cast<int>(cur.size()) - 1; j >= 0; --j) {
      const CellCoord c = cur[static_cast<std::size_t>(j)];
      SetOwner(ctx, c, new_owner);          // 0x5A04CA（工作数组为空则跳过）
      Touch(*ctx.world, c);                 // 0x5A04D8 GetCellAt
      ctx.world->SetIsoTile(c, 0);          // 0x5A04DD +0x38 = 0
      ctx.world->SetSubTile(c, 0);          // 0x5A04E0 +0x11A = 0
      ctx.world->SetLevel(c, level_30c);    // 0x5A04EE +0x11B = this->+0x30C
    }

    // 0x5A04FF `cmp ring, rings-1 / jge`：最后一圈不再往里找下一圈
    if (ring >= rings - 1) break;

    // 0x5A0507 起：下一圈 = 当前圈里**仍属于 owner** 且本圈没被戳过的邻格。
    // 向量增长步长 = cur.size()（0x5A0539），因 cur 为空时压根不进循环，
    // 「步长 0 导致 push 静默失败」这条路不可达，故这里不必建模。
    std::vector<CellCoord> next;
    for (int k = static_cast<int>(cur.size()) - 1; k >= 0; --k) {
      const CellCoord c = cur[static_cast<std::size_t>(k)];
      for (int dir = 0; dir < 8; ++dir) {   // 0x5A0560 `and eax,7`
        const CellCoord n = Step(c, dir & 7);
        if (!ctx.grid.InMap(n)) continue;   // 0x5A0593 起的四条菱形比较
        PaintWorkCell& w = Rec(ctx, n);
        if (w.blob_id != owner) continue;   // 0x5A05D6：刚剥掉的格已改成 newOwner
        if (w.queued == ring + 1) continue; // 0x5A05E5：本圈已收
        next.push_back(n);                  // 0x5A0621
        w.queued = ring + 1;                // 0x5A0628
      }
    }
    cur.swap(next);                         // 0x5A0665-0x5A0669
  }

  // 0x5A0693 `cmp esi,1 / jle`：同样只在 rings > 1 时才收拾戳
  if (rings > 1) {
    ctx.grid.ForEachCell([&](const CellCoord& c) { Rec(ctx, c).queued = 0; });
  }
  return true;                              // 0x5A06E8 `mov al,1`
}

// ── 0x5A0160 Dilate ─────────────────────────────────────────────────────
//
// 栈帧（call 0x5A0700 返回后 esp 记作 E）：
//   [E+0x30] owner  [E+0x34] rings  [E+0x38] rectX  [E+0x3C] rectY
//   [E+0x40] rectW  [E+0x44] rectH  [E+0x48] flag   [E+0x4C] cellVal
bool Dilate(RegionContext& ctx, int owner, int rings, int rect_x, int rect_y,
            int rect_w, int rect_h, bool flag, int cell_val) {
  std::vector<CellCoord> cur = CollectBorderCells(ctx, owner);   // 0x5A016C

  for (int ring = 0; ring < rings; ++ring) {   // 0x5A0181 / 0x5A03B7
    std::vector<CellCoord> next;               // 步长 = cur.size()*3（0x5A01C5）
    for (std::size_t i = 0; i < cur.size(); ++i) {      // 0x5A01CF 起
      for (int dir = 0; dir < 8; ++dir) {               // 0x5A01DA 起
        const CellCoord n = Step(cur[i], dir & 7);
        if (!ctx.grid.InMap(n)) continue;               // 0x5A022B 起四条
        // 0x5A0257-0x5A0285：再夹一层**真实坐标矩形**，四条都是半开区间
        if (n.x < rect_x || n.x >= rect_x + rect_w) continue;
        if (n.y < rect_y || n.y >= rect_y + rect_h) continue;

        PaintWorkCell& w = Rec(ctx, n);
        const int owner_n = w.blob_id;                  // 0x5A02A8
        // 0x5A02A6-0x5A02BA：bl = flag 且这一格属于「上一号」区域
        const bool bl = flag && owner_n == owner - 1;
        Touch(*ctx.world, n);                           // 0x5A02C6 GetCellAt
        const int tile = ctx.world->IsoTile(n);

        // 0x5A02CB-0x5A02ED 三段跳原样展开
        bool accept = false;
        if (owner_n == 0 || bl) {
          if (IsUndecidedTile(tile)) {                  // 0x5A02D7
            accept = true;
          } else if (bl && IsWaterOrShoreTile(tile, *ctx.theater)) {  // 0x5A02E6
            accept = true;
          }
        }
        if (!accept) {
          // 0x5A02EF：没被接受的邻格只要不是自己人，整个函数当场失败。
          // ⚠️ owner_n == 0 也走这条——「无主但砖不是未定砖」照样判失败。
          if (owner_n != owner) return false;
          continue;
        }

        next.push_back(n);                              // 0x5A0337
        w.blob_id = owner;                              // 0x5A0348
        // 0x5A034B-0x5A036B：只有 flag 且原本是未定砖，才顺手压高度层
        if (flag && IsUndecidedTile(tile)) {
          Touch(*ctx.world, n);                         // 0x5A0362 又取了一次
          ctx.world->SetLevel(n, static_cast<std::int8_t>(cell_val));
        }
      }
    }
    cur.swap(next);                                     // 0x5A03A7-0x5A03AF
  }
  return true;                                          // 0x5A03D5 `mov al,1`
}


// ── 0x5A08D0 AngularGrow ────────────────────────────────────────────────
namespace {

// [0x7E44D0] π / [0x7E3CC0] 2π / [0x7E2820] π/2 / 3π/2 立即数 0x4012D97C7F3321D2
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 6.283185307179586;
constexpr double kHalfPi = 1.5707963267948966;
constexpr double kThreeHalfPi = 4.71238898038469;
// [0x7ED790] 2.0 / [0x7ED798] 1.5，两个都是 **double**（消费指令 fmul qword）
constexpr double kJitterWeight = 2.0;
constexpr double kAngleWeight = 1.5;
// [0x7E3D88] π/4，基准角每轮游走的 sigma
constexpr double kBaseWalkSigma = 0.7853981633974483;

// 0x5A0AC4-0x5A0B53（种子那一轮）与 0x5A0E7A-0x5A0F0F（主循环）里两段
// 一模一样的代码：这一格相对 origin 的方位角，离 base 偏了多少。
//
// ⚠️ 先 fabs 再模 2π，**不是**"取角度差的主值"；差落在 (-2π, 0) 时
// fabs 已经把它镜像过去了。照抄。
double AngleDeviation(const CellCoord& c, const CellCoord& origin,
                      double base) {
  const int dx = c.x - origin.x;                  // 0x5A0AD0
  const int dy = c.y - origin.y;                  // 0x5A0AD8
  double a;
  if (dx == 0) {
    a = kHalfPi;                                  // 0x5A0BE9，double π/2
  } else {
    // 0x5A0AEA `fild dy / fidiv dx / fchs`：先整数转 double 再相除再取负
    a = Atan(-(static_cast<double>(dy) / static_cast<double>(dx)));
    if (dx < 0) a += kPi;                         // 0x5A0B06
  }
  a = a - base;                                   // 0x5A0B0C
  if (a < 0.0) a = -a;                            // 0x5A0B10 fabs
  while (!(a < kTwoPi)) a -= kTwoPi;              // 0x5A0B12-0x5A0B34
  if (a > kPi) a = kTwoPi - a;                    // 0x5A0B3A-0x5A0B4F
  return a;
}

}  // namespace

bool AngularGrow(RegionContext& ctx, int blob_id, float weight,
                 const Rect& rect, const CellCoord& origin, bool flush) {
  // 0x5A08DE / 0x5A0932：池与堆各算一次同样的 max(2*W*H, 100)
  const int cap = std::max(2 * ctx.map_w * ctx.map_h, 100);
  // ⚠️ 堆的容量判据（count+1 >= cap，0x5A0F97）必须照抄，它有观测效应；
  // 但节点池那一侧原版**没有任何越界检查**（0x5A0B76 / 0x5A0F7E 只有一句
  // `add edi,8`），池满之后就是越界写——UB，没法"复刻"。这里把池开大到
  // 走不满，于是入池永不失败，行为等同于原版没有溢出的那条路径。
  // 上界：种子一格最多入一次，邻格入池时会写 +0x3C 去重，故至多 k + 格数。
  NodePool pool(cap + 2 * (ctx.map_w + 1) * (ctx.map_h + 1) + 16);
  MinHeap heap(&pool, cap);

  // 0x5A099D-0x5A0A03：两组 if **顺序执行**，第二组无条件覆盖第一组的结论。
  // 只要 rect.y != 0，基准角一定是 3π/2，第一组白算。照抄。
  double base = 0.0;
  if (rect.x == 0) {
    if (rect.w != 0x200) base = kPi;
  } else {
    base = 0.0;
  }
  if (rect.y != 0) {
    base = kThreeHalfPi;
  } else {
    if (rect.h != 0x200) base = kHalfPi;
  }

  // 0x5A0A03-0x5A0A4E：入堆标记全图清零
  ctx.grid.ForEachCell([&](const CellCoord& c) { Rec(ctx, c).queued = 0; });

  // 0x5A0A58：种子 = blobId 的全部边界格；k 还要拿去算迭代次数
  const std::vector<CellCoord> seeds = CollectBorderCells(ctx, blob_id);
  const int k = static_cast<int>(seeds.size());

  auto push_node = [&](const CellCoord& c) {
    const double a = AngleDeviation(c, origin, base);
    const double u =
        static_cast<double>(ctx.rng->Random()) * kInvUintMax;   // [0x7ED898]
    // 0x5A0B7F-0x5A0B9F：1.5*a 与 2.0*u 都是 double 乘，加完才落成 float
    const float prio =
        static_cast<float>(kAngleWeight * a + kJitterWeight * u);
    const int node = pool.Alloc(c.x, c.y, prio);
    if (node >= 0) heap.Push(node);
  };

  // 0x5A0A78-0x5A0C28：种子入堆。⚠️ 这一轮**不写** work.+0x3C
  for (int i = 0; i < k; ++i) {
    const CellCoord c = seeds[static_cast<std::size_t>(i)];
    if (c.x < rect.x || c.x >= rect.x + rect.w) continue;
    if (c.y < rect.y || c.y >= rect.y + rect.h) continue;
    push_node(c);
  }

  // 0x5A0C40-0x5A0C74：迭代次数。那串倒数**不能约掉**——
  // `1.0/L`、`*weight`、`1.0f/(...)` 三步各引入一次舍入。
  const double ln_k = ChopLog(static_cast<double>(k));
  const double L = (1.0 <= ln_k) ? ln_k : 1.0;      // 0x5A0C4E fcomp + test ah,0x41
  const double t1 = 1.0 / L;                        // 0x5A0C5F fdivr qword 1.0
  const double t2 = t1 * static_cast<double>(weight);           // 0x5A0C65
  const double t3 = static_cast<double>(1.0f) / t2;             // 0x5A0C68
  int total = Ftol(t3 * static_cast<double>(0.5f));             // 0x5A0C6E/0x5A0C74
  // 0x5A0C7D `cdq / sub eax,edx / sar eax,1`：向零取整的 n/2
  const int half = (total - (total >> 31)) >> 1;
  total += PickBelowMul(*ctx.rng, half + 1);        // 0x5A0C99-0x5A0CC0

  bool ok = true;                                   // [esp+0x0F]
  int cur = heap.empty() ? -1 : heap.Pop();         // 0x5A0CCC：进循环前先弹一个

  for (int i = 0; i < total; ++i) {                 // 0x5A0D0A / 0x5A1095
    if (!ok) break;                                 // 0x5A0D18
    if (cur < 0) break;                             // 0x5A0D24
    const CellCoord at{pool[cur].x, pool[cur].y};

    // 0x5A0D4A-0x5A0D6E：认领当前格
    PaintWorkCell& w = Rec(ctx, at);
    if (w.blob_id == 0) {
      Touch(*ctx.world, at);                        // 0x5A0D57 GetCellAt
      if (IsUndecidedTile(ctx.world->IsoTile(at))) {
        w.flag4b = true;                            // 0x5A0D6A +0x4B = 1
        w.blob_id = blob_id;                        // 0x5A0D6E
      }
    }

    // ⚠️ 0x5A102E `add eax,2`：步长 2，只看四个正方向
    for (int d = 0; d < 8; d += 2) {
      const CellCoord n = Step(at, d & 7);          // 0x5A0DA0 = 0x42D510
      if (!ctx.grid.InMap(n)) continue;             // 0x5A0DC0 起四条
      PaintWorkCell& wn = Rec(ctx, n);
      bool accepted = false;
      if (wn.blob_id == 0 && wn.queued != blob_id &&
          n.x >= rect.x && n.x < rect.x + rect.w &&
          n.y >= rect.y && n.y < rect.y + rect.h) {
        Touch(*ctx.world, n);                       // 0x5A0E62
        if (IsUndecidedTile(ctx.world->IsoTile(n))) {
          wn.queued = blob_id;                      // 0x5A0F6A
          push_node(n);
          accepted = true;
        }
      }
      if (accepted) continue;                       // 入堆成功就不做下面的判据
      // 0x5A0FF6-0x5A1025：没入堆的邻格再查一次归属
      if (wn.blob_id == 0) continue;
      if (wn.blob_id != blob_id) ok = false;        // 撞上第三方地盘
    }

    // 0x5A104C：基准角每轮随机游走
    base += (*ctx.normal)() * kBaseWalkSigma;
    cur = heap.empty() ? -1 : heap.Pop();           // 0x5A1057-0x5A1091
  }

  // 0x5A10A5-0x5A115D：收尾。⚠️ 这一段认领时**不查砖**
  if (flush) {
    while (!heap.empty()) {
      cur = heap.Pop();
      if (cur < 0) break;
      if (!ok) break;
      const CellCoord at{pool[cur].x, pool[cur].y};
      PaintWorkCell& w = Rec(ctx, at);
      if (w.blob_id == 0) {
        w.blob_id = blob_id;                        // 0x5A111D，无条件
      } else if (w.blob_id != blob_id) {
        ok = false;                                 // 0x5A1128
      }
    }
  }
  return ok;                                        // 0x5A1179
}

}  // namespace rmg
}  // namespace ra2
