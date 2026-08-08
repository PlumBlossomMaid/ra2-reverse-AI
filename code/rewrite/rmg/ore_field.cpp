#include "ore_field.h"

#include <cstdint>
#include <cstdio>

#include "cell_ref.h"      // kAdjacentDelta（0x89F688）
#include "place_tile.h"    // IsUndecidedTile（0x486380）
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

// 池子里的一条，原版 8 字节：{ int32 packedXY; float key }。
// 坐标必须是 16 位——0x5A2D45 起用 movsx 从这里读回来算距离。
struct OreNode {
  std::int16_t x = 0;
  std::int16_t y = 0;
  float key = 0.0f;
};

// 一基二叉最小堆。与 blob_heap.h 的 MinHeap 是同一个模板（上浮内联在
// 0x5A2AD0 一带，下沉 0x5AD870 就是同一个函数），三处不同：
//   · 多一个「清堆」入口（0x5A2903 / 0x5A2947 / 0x5A2B4E 三处一模一样：
//     把 arr[0..count] 全写 0 再把 count 归零）
//   · 数组里存的是**池子里的槽位**，而槽位会被后面的 used = 0 覆写——
//     所以比较时必须当场去池子里取 key，不能提前拷一份
//   · 容量是 count*10，数组是 count*0x28+4 字节 = cap+1 个 dword
class OreHeap {
 public:
  OreHeap(const std::vector<OreNode>* pool, int cap)
      : pool_(pool), cap_(cap), slot_(static_cast<std::size_t>(cap) + 1, -1) {}

  int size() const { return count_; }

  void Clear() {
    for (int i = 0; i <= count_; ++i) slot_[static_cast<std::size_t>(i)] = -1;
    count_ = 0;
  }

  // 0x5A2AD0 / 0x5A2DE0 两处内联的上浮。门槛 count + 1 >= cap，于是最后一格
  // 永远用不上；越界时**静默丢弃**，而调用方已经把 work[].+0x3C 写成 fieldId
  // 了，不回退。
  bool Push(int node) {
    int i = count_ + 1;
    if (i >= cap_) return false;
    const float pr = Key(node);
    int parent = static_cast<int>(static_cast<unsigned>(i) >> 1);
    while (i > 1) {
      const int up = slot_[static_cast<std::size_t>(parent)];
      // fcomp + test ah,0x41：父 <= 新值就停，相等也停
      if (!(Key(up) > pr)) break;
      slot_[static_cast<std::size_t>(i)] = up;
      i = parent;
      parent = static_cast<int>(static_cast<unsigned>(i) >> 1);
    }
    slot_[static_cast<std::size_t>(i)] = node;
    ++count_;
    // piVar3[3] / piVar3[4] 那两个「最大/最小指针」统计只写不读，略去
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

  // 0x5AD870，与涂块共用同一个函数。全部比较都是**严格大于**。
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
  float Key(int node) const { return (*pool_)[static_cast<std::size_t>(node)].key; }

  const std::vector<OreNode>* pool_;
  int cap_;
  int count_ = 0;
  std::vector<int> slot_;
};

}  // namespace

void CreateOreField(OreContext& ctx, const CellCoord& seed, int count,
                    int field_id, bool is_gems) {
  OreWorld& world = *ctx.world;
  std::vector<PaintWorkCell>& work = *ctx.work;
  const int side = ctx.side;

  // operator new(count * 0x50) = count*10 条 8 字节的节点。
  // ⚠️ 紧接着算出来的 count*10 - 1（存进 local_40）**一次都没被读**——池子没有
  // 上界检查。用光之后原版会一路写到分配区外面（紧邻的多半就是堆数组），
  // 那是真·未定义行为，逐字节复刻不了；这里让池子自然增长，并在此备案。
  const int cap = count * 10;
  std::vector<OreNode> pool(static_cast<std::size_t>(cap > 0 ? cap : 0));
  OreHeap heap(&pool, cap);

  int used = 0;                                     // local_64
  CellCoord origin = seed;                          // local_68
  bool anchored = false;                            // bVar1
  // 0x5A2953 neg cl / sbb ecx,ecx / and ecx,0xFFFFFFB5 / add ecx,0x66
  // ——矿基址 102（Riparius 102..113）、宝石基址 27（Cruentus 27..38）
  const int base = is_gems ? 27 : 102;
  int placed = 0;                                   // local_60
  int restarts = 0;                                 // local_5c
  heap.Clear();                    // 0x5A2988：分配时清过一遍，这里又清一遍
  int cur = -1;                                     // local_74 = NULL

  if (count > 0) {
    do {
      if (restarts > 9) break;                      // 0x5A29B0 if (9 < local_5c)
      if (cur < 0) {
        // —— 冷启动 / 扩散断了：整片田从种子格重来 ——
        heap.Clear();
        // 0x5A29E0：**整整一遍全图迭代器**，把每格的工作记录 +0x3C 清 0。
        // 最坏一片矿田要清 10 次全图。别改成增量清理——清的时机会影响下面
        // queued != field_id 那个判定。
        ctx.grid.ForEachCell([&](const CellCoord& c) {
          work[static_cast<std::size_t>(c.y * side + c.x)].queued = 0;
        });
        used = 1;
        pool[0].x = static_cast<std::int16_t>(seed.x);
        pool[0].y = static_cast<std::int16_t>(seed.y);
        pool[0].key = 0.0f;
        work[static_cast<std::size_t>(seed.y * side + seed.x)].queued = field_id;
        heap.Push(0);
        cur = heap.Pop();
        ++restarts;
        origin = seed;
        anchored = false;
      }

      // 0x5A2BE9 GetCellAt(cur)：当前格的指针被缓存下来给后面的 overlay 写入，
      // 所以这里取一次坐标就够；邻域循环里则**每个方向都重取**（见下）。
      const CellCoord cc{pool[static_cast<std::size_t>(cur)].x,
                         pool[static_cast<std::size_t>(cur)].y};

      if (!work[static_cast<std::size_t>(cc.y * side + cc.x)].flag45) {
        if (!anchored) {
          // 本次爆发的第一格：把距离原点搬到这里，池子与堆全部作废
          origin = cc;
          anchored = true;
          heap.Clear();
          // ⚠️ used 归零但池子内存不清。cur 指着的那一条紧接着就会被下面第一个
          // 邻居覆写（冷启动后 cur 恒是 0 号槽，必然踩中）——于是同一轮邻域
          // 循环里，后几个方向算的已经是**新坐标**的邻居了。照抄。
          used = 0;
          if (!is_gems) {
            // 0x5A2B8E：折叠常数 3/(2^32-1) 再 fadd 1.0，拒绝 > 3
            const int i = PickFrom1(*ctx.rng, 3);
            char name[24];
            std::sprintf(name, "TIBTRE0%d", i % 10);   // 0x82C090
            world.CreateTerrain(name, origin);
          }
        }
        if (world.Overlay(cc) == -1) {
          const int v = PickBelow(*ctx.rng, 12);       // 0x5A2C18
          world.SetOverlay(cc, v + base);
          ++placed;
        } else if (world.OverlayData(cc) > 10) {
          // 0x5A2C61 goto LAB_005A2C67：吃满的老矿格既不加厚也不计入 placed，
          // 但下面 8 个邻居照样展开——一片满田会把预算白白拖着转
        } else {
          world.SetOverlayData(cc, world.OverlayData(cc) + 1);
          ++placed;
        }
      }

      // 0x5A2C73 lea edi, [edi + eax*8]：写指针 = 池底 + used
      int d = 0;
      do {
        // 0x5A2C7F 起：邻接表 [0x89F688]，下标 d & 7，坐标按 **int16** 相加
        const CellCoord n{
            static_cast<std::int16_t>(pool[static_cast<std::size_t>(cur)].x +
                                      kAdjacentDelta[d & 7].x),
            static_cast<std::int16_t>(pool[static_cast<std::size_t>(cur)].y +
                                      kAdjacentDelta[d & 7].y)};
        // 0x5A2CB5 IsWithinUsableArea(&n, 1)：第二个参数是 use_height，于是它
        // 会先取格子读 +0x11B/+0x11C（出图就读到哑元里上一次的残留值）
        if (IsWithinUsableArea(ctx.map_w, ctx.local, n.x, n.y, world.Level(n),
                               world.Ramp(n), true)) {
          if (IsUndecidedTile(world.IsoTile(n))) {     // 0x5A2CD9 = 0x486380
            // 0x5A2CE6 起的两条接受条件。⚠️ 第二条**没有** fieldId 保护，
            // 于是同一格可以被反复入队——池子就是这么被撑爆的。
            if ((world.Overlay(n) == -1 &&
                 work[static_cast<std::size_t>(n.y * side + n.x)].queued !=
                     field_id) ||
                (world.OverlayData(n) < 11 &&
                 world.ContainedTiberiumIndex(n) != -1)) {
              if (used >= static_cast<int>(pool.size())) {
                pool.resize(static_cast<std::size_t>(used) + 1);  // 见上文备案
              }
              OreNode& slot = pool[static_cast<std::size_t>(used)];
              slot.x = static_cast<std::int16_t>(n.x);            // 0x5A2D40
              slot.y = static_cast<std::int16_t>(n.y);
              // 0x5A2D45 起：距离的原点是 origin，不是 seed
              const int dx = origin.x - n.x;
              const int dy = origin.y - n.y;
              const double dist =
                  FastSqrt(static_cast<double>(dy * dy + dx * dx));
              // 0x5A2D8F 起：fild r / fmul 5.0 / fmul 1/(2^32-1) / fadd dist，
              // 全程 double，最后 fstp dword 落成 float
              const double jitter =
                  static_cast<double>(
                      static_cast<std::int64_t>(ctx.rng->Random())) *
                  5.0 * kInvUintMax;
              slot.key = static_cast<float>(jitter + dist);
              work[static_cast<std::size_t>(n.y * side + n.x)].queued = field_id;
              heap.Push(used);
              ++used;
            }
          }
        }
        ++d;
      } while (d < 8);

      cur = heap.Pop();
    } while (placed < count);
  }
}

void PlaceTiberium(OreContext& ctx, RegionSet& regions,
                   const TiberiumParams& params) {
  OreWorld& world = *ctx.world;

  // 0x5A23A6 起那张真值表
  bool do_pick = false;
  switch (params.map_type) {
    case 0:
      do_pick = (params.resources == 3);
      break;
    case 1:
    case 3:
    case 4:
      do_pick = (params.resources != 3);
      break;
    case 2:
      do_pick = true;
      break;
    default:
      do_pick = false;
      break;
  }

  int wp_base = 0;                                   // local_80
  for (int ri = 0; ri < regions.Count(); ++ri) {
    RegionClass* r = regions.At(ri);
    // 0x5A23F1 if (*piVar1 != 0) 判的是**候选点向量的指针**。阶段 6 只在真的
    // 挑出候选点时才建那个向量，所以这里拿「空向量」代表空指针。
    if (r->candidates.empty()) continue;
    const int cand_count = static_cast<int>(r->candidates.size());

    int special = -1;                                // local_6c
    if (do_pick) {
      std::int16_t ref_x = 0, ref_y = 0;
      if (r->start_points >= 1) {
        // 0x5A2459 add bp, word[esp+0x52] / 0x5A245E add bx, ax：
        // 累加器是 **16 位**，出生点多、坐标大时会绕回。除法前才 movsx。
        std::int16_t sx = 0, sy = 0;
        for (int i = 0; i < r->start_points; ++i) {
          const CellCoord wp = world.Waypoint(i + wp_base);
          sx = static_cast<std::int16_t>(sx + wp.x);
          sy = static_cast<std::int16_t>(sy + wp.y);
        }
        // 0x5A246C movsx eax, bx / cdq / idiv ecx，商再截回 16 位
        ref_x = static_cast<std::int16_t>(sx / r->start_points);
        ref_y = static_cast<std::int16_t>(sy / r->start_points);
      } else {
        // 0x5A2494：**两次乘法**那一路（fild n / fmul r / fmul 1/(2^32-1)），
        // 与折叠常数的 PickBelow 在末位会分岔，不能互换
        const int k = PickBelowMul(*ctx.rng, static_cast<int>(r->cells.size()));
        ref_x = static_cast<std::int16_t>(r->cells[static_cast<std::size_t>(k)].x);
        ref_y = static_cast<std::int16_t>(r->cells[static_cast<std::size_t>(k)].y);
      }
      // 0x5A24D3：哨兵 500000，且每轮都把距离**截断成整数**再参与下一轮比较
      int best = 500000;
      for (int i = 0; i < cand_count; ++i) {
        const CellCoord c = r->candidates[static_cast<std::size_t>(i)];
        const int dy = c.y - ref_y;
        const int dx = c.x - ref_x;
        const double dist = FastSqrt(static_cast<double>(dy * dy + dx * dx));
        if (dist < static_cast<double>(best)) {   // fcompp + test ah,1：严格小于
          best = Ftol(dist);
          special = i;
        }
      }
    }

    // 0x5A2558 起：矿量参数当百分比，在 [min, max] 之间插值
    const int v = Ftol(static_cast<double>(params.tiberium) * 0.01 *
                           static_cast<double>(params.max_tiberium -
                                               params.min_tiberium) +
                       static_cast<double>(params.min_tiberium));
    // 0x5A257A 起 fcomp 0.5 + test ah,0x41：没有出生点的区域按 0.5 倍算，不是 0
    double mult = static_cast<double>(r->start_points);
    if (mult < 0.5) mult = 0.5;
    const int total = Ftol(static_cast<double>(v) * mult);
    // 0x5A25AC / 0x5A25B4：这两条一跳就跳到区域末尾——连 wpBase 都不推进
    if (cand_count == 0 || total == 0) continue;
    const int avg = total / cand_count;              // 0x5A25BB cdq/idiv

    for (int i = 0; i < cand_count; ++i) {
      // 0x5A25C9：sigma = 50 的正态抖动，接受窗口是**闭区间** [-100, +100]。
      // NormalDeviate 的缓存跨阶段串，第一次抽可能一个随机数都不花。
      double g;
      do {
        g = (*ctx.normal)() * 50.0;
      } while (g < -100.0 || g > 100.0);
      const int size = Ftol(static_cast<double>(avg) + g);
      // 0x5A2608 cmp eax, ebx / jl：size < 0 时跳过这个候选点，但随机数已经花了
      if (size >= 0) {
        CreateOreField(ctx, r->candidates[static_cast<std::size_t>(i)], size,
                       i + wp_base + 1, i == special);
      }
    }

    // 0x5A2640 起：每个出生点到候选点群的平均距离
    std::vector<double> avg_dist;                 // DynamicVectorClass<double>
    for (int w = 0; w < r->start_points; ++w) {
      const CellCoord wp = world.Waypoint(w + wp_base);
      double s = 0.0;
      for (int i = 0; i < cand_count; ++i) {
        const CellCoord c = r->candidates[static_cast<std::size_t>(i)];
        const int dy = c.y - wp.y;
        const int dx = c.x - wp.x;
        s = FastSqrt(static_cast<double>(dy * dy + dx * dx)) + s;
      }
      avg_dist.push_back(s / static_cast<double>(cand_count));
    }
    double mn = 9999999.0;                           // 0x416312CFE0000000
    for (int w = 0; w < r->start_points; ++w) {
      if (avg_dist[static_cast<std::size_t>(w)] < mn) {
        mn = avg_dist[static_cast<std::size_t>(w)];
      }
    }

    // 0x5A27B0 起：只有这条分支才布宝石
    const bool is_gems =
        (params.resources == 3 &&
         (params.map_type == 1 || params.map_type == 3 || params.map_type == 4));

    for (int w = 0; w < r->start_points; ++w) {
      // 0x5A27F4-0x5A280A：离矿区最近的那个出生点恰好拿 500 格，每远 1 格多 15
      const int size =
          Ftol((avg_dist[static_cast<std::size_t>(w)] - mn) * 15.0) + 500;
      const CellCoord wp = world.Waypoint(wp_base + w);
      CreateOreField(ctx, wp, size, wp_base + w + 1, is_gems);
    }
    wp_base += r->start_points;
  }
}

}  // namespace rmg
}  // namespace ra2
