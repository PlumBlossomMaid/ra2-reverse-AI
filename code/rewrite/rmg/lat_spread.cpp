// 阶段 10 的两个「最小堆扩散」：LAT 块写入器 0x5A4B60 与树林补丁 0x5A45E0。
// 与涂块 0x59BBC0、矿田 0x5A28C0 是同一个模子，但每处的接受条件、
// 边界检查、以及「用光池子会怎样」各不相同，所以各写一份，不抽公共模板。
#include "lats.h"

#include <cstdlib>
#include <cstdint>
#include <cstdio>

#include "cell_ref.h"      // kAdjacentDelta（0x89F688）
#include "place_tile.h"    // IsUndecidedTile（0x486380）
#include "scaled_random.h"
#include "x87_fyl2x.h"

namespace ra2 {
namespace rmg {
namespace {

struct TraceRange {
  bool enabled = false;
  long long begin = 0;
  long long end = 0;
};

TraceRange LatTraceRange() {
  TraceRange r;
  const char* s = std::getenv("RA2_RMG_LAT_TRACE");
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

bool TraceNextRandom(const Randomizer& rng, const TraceRange& tr) {
  if (!tr.enabled) return false;
  const long long call = rng.call_count() + 1;
  return call >= tr.begin && call <= tr.end;
}

bool TracePossibleBlock(const Randomizer& rng, const TraceRange& tr, int count) {
  if (!tr.enabled) return false;
  const long long before = rng.call_count();
  const long long rough_max = before + static_cast<long long>(count) * 8 + 8;
  return before <= tr.end && rough_max >= tr.begin;
}

// 池子里的一条，原版 8 字节：{ int16 x; int16 y; float key }。
struct LatNode {
  std::int16_t x = 0;
  std::int16_t y = 0;
  float key = 0.0f;
};

// 一基二叉最小堆。堆结构本体是 new(0x14) 的五个 dword：
//   [0] count  [1] cap  [2] 数组  [3] 最大指针  [4] 最小指针
// 后两个只写不读，略去。数组是 cap+1 个 dword。
class LatHeap {
 public:
  LatHeap(const std::vector<LatNode>* pool, int cap)
      : pool_(pool), cap_(cap),
        slot_(static_cast<std::size_t>(cap > 0 ? cap : 0) + 1, -1) {}

  int size() const { return count_; }

  // 上浮。门槛 count + 1 >= cap（无符号比较），于是最后一格永远用不上；
  // 越界时**静默丢弃**，而调用方已经把标记字节写下去了，不回退。
  bool Push(int node) {
    unsigned i = static_cast<unsigned>(count_) + 1u;
    if (i >= static_cast<unsigned>(cap_)) return false;
    const float pr = Key(node);
    while (i > 1u) {
      const int up = slot_[i >> 1];
      // fcomp + test ah,0x41：父 <= 新值就停，相等也停
      if (!(Key(up) > pr)) break;
      slot_[i] = up;
      i >>= 1;
    }
    slot_[i] = node;
    ++count_;
    return true;
  }

  // 取 arr[1]，末项搬到根，末槽清零，count--，再 SiftDown(1)。
  // 空堆返回 -1（原版返回 NULL，随后被当成「扩散断了」）。
  int Pop() {
    if (count_ == 0) return -1;
    const int top = slot_[1];
    slot_[1] = slot_[static_cast<std::size_t>(count_)];
    slot_[static_cast<std::size_t>(count_)] = -1;
    --count_;
    SiftDown(1);
    return top;
  }

  // 0x5AD870，四段扩散共用同一个函数。全部比较都是**严格大于**。
  void SiftDown(int i) {
    for (;;) {
      const int l = 2 * i, r = 2 * i + 1;
      int best = i;
      if (l <= count_ && Key(slot_[static_cast<std::size_t>(i)]) >
                             Key(slot_[static_cast<std::size_t>(l)])) {
        best = l;
      }
      if (r <= count_ && Key(slot_[static_cast<std::size_t>(best)]) >
                             Key(slot_[static_cast<std::size_t>(r)])) {
        best = r;
      }
      if (best == i) return;
      const int t = slot_[static_cast<std::size_t>(i)];
      slot_[static_cast<std::size_t>(i)] = slot_[static_cast<std::size_t>(best)];
      slot_[static_cast<std::size_t>(best)] = t;
      i = best;
    }
  }

 private:
  float Key(int node) const {
    return (*pool_)[static_cast<std::size_t>(node)].key;
  }

  const std::vector<LatNode>* pool_;
  int cap_;
  int count_ = 0;
  std::vector<int> slot_;
};

// 两处抖动完全同形（常量分别是 [0x7ED808] 与 [0x7ED800]，都是 5.0，
// 原版用 x87 80位扩展精度做中间乘加，最后 fstp dword（chop）到 float。
// x64 的 double（53位）中间精度差 2^-53 量级 → 偶尔导致 float 差 1 ULP
// → 翻转 heap Pop 顺序 → 扩散路径分岔 → trees 750 差异。
// 修法：在 x64 上用 x87 机器码执行完整 SpreadKey 计算链（80位精度）。
float SpreadKey(double dist, std::uint32_t r) {
  return X87SpreadKey(dist, r);
}

}  // namespace

void WriteLatBlock(LatContext& ctx, const CellCoord& seed, int tile_index,
                   int count, int field_id, bool allow_non_plain) {
  LatWorld& world = *ctx.world;
  std::vector<PaintWorkCell>& work = *ctx.work;
  const int side = ctx.side;
  const TraceRange trace = LatTraceRange();
  const bool trace_block = TracePossibleBlock(*ctx.rng, trace, count);
  if (trace_block) {
    std::fprintf(stderr,
                 "LATTRACE block before=%lld seed=(%d,%d) tile=%d count=%d "
                 "field=%d allow=%d\n",
                 ctx.rng->call_count(), seed.x, seed.y, tile_index, count,
                 field_id, allow_non_plain ? 1 : 0);
  }

  // operator new(count * 0x50) = count*10 条 8 字节的节点；堆容量也是 count*10。
  // ⚠️ 这里**没有**池子上界检查（不像 0x5A45E0 那条 used < cap），
  // 但每弹出一格最多推 8 个邻居、总共只弹 count 次，used <= 8*count + 1，
  // 而 count >= 4（调用点 TruncNormal 的下界），所以池子实际越不了界。
  const int cap = count * 10;
  std::vector<LatNode> pool(static_cast<std::size_t>(cap > 0 ? cap : 0));
  LatHeap heap(&pool, cap);

  if (cap <= 0) return;
  int used = 1;                                     // local_30
  pool[0].x = static_cast<std::int16_t>(seed.x);
  pool[0].y = static_cast<std::int16_t>(seed.y);
  pool[0].key = 0.0f;
  // 种子格的「本田已收」标记用 +0x3C，值就是 fieldId
  work[static_cast<std::size_t>(seed.y * side + seed.x)].queued = field_id;
  heap.Push(0);
  int node = heap.Pop();

  if (count <= 0) return;
  int placed = 0;                                   // local_24
  do {
    if (node < 0) break;                            // 扩散断了
    const CellCoord at{pool[static_cast<std::size_t>(node)].x,
                       pool[static_cast<std::size_t>(node)].y};
    if (trace_block) {
      std::fprintf(stderr,
                   "LATTRACE place before=%lld placed=%d at=(%d,%d) "
                   "node=%d key=%.9g\n",
                   ctx.rng->call_count(), placed, at.x, at.y, node,
                   static_cast<double>(pool[static_cast<std::size_t>(node)].key));
    }
    world.SetIsoTile(at, tile_index);               // ← 真正的地形写入
    // ⚠️ 2026-10-04 探针：逐格 tile 写入 trace
    if (std::getenv("RA2_RMG_WLB_TRACE")) {
      std::fprintf(stderr, "WLBPLACE rng=%lld at=(%d,%d) tile=%d\n",
                   ctx.rng->call_count(), at.x, at.y, tile_index);
    }

    for (int dir = 0; dir < 8; ++dir) {
      // 0x42D510 CellStruct 相加，坐标按 int16
      const CellCoord n{
          static_cast<std::int16_t>(at.x + kAdjacentDelta[dir & 7].x),
          static_cast<std::int16_t>(at.y + kAdjacentDelta[dir & 7].y)};
      if (!ctx.grid.InMap(n)) continue;
      bool ok = IsUndecidedTile(world.IsoTile(n));
      if (!ok && allow_non_plain) {
        // 0x486670 / 0x486650 两个「铺装地面」谓词，原版就是这样接着写的
        // （前者为真则后者不再调用）。阶段 10 的三个调用点 allowNonPlain
        // 全传 0，所以这是死分支——形状保留。
        ok = world.IsPaveTile(n) || world.IsMiscPaveTile(n);
      }
      if (!ok) continue;
      if (work[static_cast<std::size_t>(n.y * side + n.x)].queued == field_id) continue;
      if (world.Ramp(n) != 0) continue;
      if (world.Overlay(n) != -1) continue;
      if (world.Field0xE4(n) != 0) continue;

      const int dx = seed.x - n.x;
      const int dy = seed.y - n.y;
      const double dist = FastSqrt(static_cast<double>(dx * dx + dy * dy));
      const bool trace_random = TraceNextRandom(*ctx.rng, trace);
      const long long call = ctx.rng->call_count() + 1;
      const std::uint32_t draw = ctx.rng->Random();
      const float key = SpreadKey(dist, draw);
      // ⚠️ 2026-10-04 探针：SpreadKey 的 double 输入 vs float 输出（bit 级）
      if (std::getenv("RA2_RMG_KEY_TRACE")) {
        const double raw = static_cast<double>(
            static_cast<std::uint64_t>(draw)) * 5.0 * kInvUintMax + dist;
        std::fprintf(stderr,
                     "KEYTRACE call=%lld n=(%d,%d) dist=%.17g draw=%u "
                     "raw=%.17g key=0x%08X key_d=%.9g\n",
                     call, n.x, n.y, dist, draw, raw,
                     *reinterpret_cast<const std::uint32_t*>(&key),
                     static_cast<double>(key));
      }
      if (trace_block || trace_random) {
        std::fprintf(stderr,
                     "LATTRACE push call=%lld used=%d from=(%d,%d) "
                     "n=(%d,%d) dir=%d dist=%.17g draw=%u key=%.9g "
                     "tile=%d count=%d field=%d\n",
                     call, used, at.x, at.y, n.x, n.y, dir, dist, draw,
                     static_cast<double>(key), tile_index, count, field_id);
      }
      pool[static_cast<std::size_t>(used)].x = static_cast<std::int16_t>(n.x);
      pool[static_cast<std::size_t>(used)].y = static_cast<std::int16_t>(n.y);
      pool[static_cast<std::size_t>(used)].key = key;
      work[static_cast<std::size_t>(n.y * side + n.x)].queued = field_id;
      heap.Push(used);
      ++used;
    }

    ++placed;
    node = heap.Pop();
  } while (placed < count);
  if (trace_block) {
    std::fprintf(stderr, "LATTRACE block_end after=%lld placed=%d used=%d\n",
                 ctx.rng->call_count(), placed, used);
  }
}

int GrowTreePatch(LatContext& ctx, const CellCoord& seed, int size, double prob) {
  LatWorld& world = *ctx.world;
  std::vector<PaintWorkCell>& work = *ctx.work;
  const int side = ctx.side;

  const bool gtrace = std::getenv("RA2_RMG_TREES_TRACE") != nullptr;
  const long long rng_in = gtrace ? ctx.rng->call_count() : 0;

  // operator new(size * 200) = size*25 条；堆容量 [1] 也是 size*25。
  const int cap = size * 25;
  std::vector<LatNode> pool(static_cast<std::size_t>(cap > 0 ? cap : 0));
  LatHeap heap(&pool, cap);

  if (cap <= 0) return 0;
  int used = 1;                                     // local_58
  pool[0].x = static_cast<std::int16_t>(seed.x);
  pool[0].y = static_cast<std::int16_t>(seed.y);
  pool[0].key = 0.0f;
  // 这里的「走过了」标记用 +0x47，**不是** +0x3C
  work[static_cast<std::size_t>(seed.y * side + seed.x)].flag47 = true;
  heap.Push(0);
  int node = heap.Pop();

  int planted = 0;                                  // local_54
  if (size <= 0) return planted;
  do {
    if (node < 0 || cap <= used) break;
    const CellCoord at{pool[static_cast<std::size_t>(node)].x,
                       pool[static_cast<std::size_t>(node)].y};
    if (IsUndecidedTile(world.IsoTile(at)) && world.Field0xE4(at) == 0 &&
        world.Overlay(at) == -1 && world.Field0xEC(at) != 3) {
      // 取数点 0x5A47F2：r * 1/(2^32-1) < prob 才栽；**失败也烧随机数**
      const std::uint32_t u_draw = ctx.rng->Random();
      const double u =
          static_cast<double>(static_cast<std::uint64_t>(u_draw)) *
          kInvUintMax;
      if (gtrace) {
        std::fprintf(stderr, "TREEDRAW at=(%d,%d) draw=%u u=%.17g prob=%.17g "
                             "pass=%d ground=%d\n",
                     at.x, at.y, u_draw, u, prob, u < prob ? 1 : 0,
                     world.Field0xEC(at));
      }
      if (u < prob) {
        // 取数点 0x5A481C：_ftol(r * 25/(2^32-1) + 1.0)，拒绝 > 25
        // → 下界是 1，所以只会是 TREE01..TREE25，**TREE00 永不出现**
        const int idx = PickFrom1(*ctx.rng, 25);
        char name[24];
        std::snprintf(name, sizeof(name), "TREE%d%d", idx / 10, idx % 10);
        world.CreateTerrain(name, at);
        ++planted;
        if (std::getenv("RA2_RMG_TREES_TRACE")) {
          std::fprintf(stderr, "TREEPOS %d %s %d %d\n",
                       planted, name, at.x, at.y);
        }
      }
    }

    for (int dir = 0; dir < 8; ++dir) {
      const CellCoord n{
          static_cast<std::int16_t>(at.x + kAdjacentDelta[dir & 7].x),
          static_cast<std::int16_t>(at.y + kAdjacentDelta[dir & 7].y)};
      if (!ctx.grid.InMap(n)) continue;
      PaintWorkCell& w = work[static_cast<std::size_t>(n.y * side + n.x)];
      if (w.flag47) continue;      // 已走过——**跨补丁不清**，所以补丁互斥
      if (w.flag45) continue;      // 已锁定
      if (used >= cap) continue;   // ⚠️ 这条上界检查这里是有的

      const int dx = seed.x - n.x;
      const int dy = seed.y - n.y;
      const double dist = FastSqrt(static_cast<double>(dx * dx + dy * dy));
      pool[static_cast<std::size_t>(used)].x = static_cast<std::int16_t>(n.x);
      pool[static_cast<std::size_t>(used)].y = static_cast<std::int16_t>(n.y);
      // used 在算 key 之前就自增了（0x5A49B0 的 inc），写的还是原来那一槽
      const int slot = used;
      ++used;
      const std::uint32_t tree_draw = ctx.rng->Random();
      const float tree_key = SpreadKey(dist, tree_draw);
      pool[static_cast<std::size_t>(slot)].key = tree_key;
      if (std::getenv("RA2_RMG_TREES_TRACE")) {
        std::fprintf(stderr, "TREEKEY %d %d %d %u %08X\n",
                     slot, n.x, n.y, tree_draw,
                     *reinterpret_cast<const std::uint32_t*>(&tree_key));
      }
      w.flag47 = true;
      heap.Push(slot);
    }

    node = heap.Pop();
  } while (planted < size);
  if (gtrace) {
    std::fprintf(stderr, "GPATCH seed=(%d,%d) size=%d prob=%.17g planted=%d "
                         "rng=%lld..%lld (%lld draws)\n",
                 seed.x, seed.y, size, prob, planted, rng_in,
                 ctx.rng->call_count(), ctx.rng->call_count() - rng_in);
  }
  return planted;
}

}  // namespace rmg
}  // namespace ra2
