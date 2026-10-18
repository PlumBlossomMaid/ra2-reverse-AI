#include "dead_code_placer.h"

#include <cstdint>
#include <string>

#include "scaled_random.h"   // Ftol / kInvUintMax

namespace ra2 {
namespace rmg {

// 0x7EDAB0 = 5/(2^32-1)、0x7E48E0 = 8.0（选数量）、0x7EDB18 = 15/(2^32-1)
// （±7 邻域）。golden 实证：golden 侧折叠常数与 n/4294967295.0 逐位一致。
static constexpr double kCountScale = 5.0 / 4294967295.0;   // 0x7EDAB0
static constexpr double kCountBias = 8.0;                    // 0x7E48E0
static constexpr double kOffsetScale = 15.0 / 4294967295.0;  // 0x7EDB18

void DeadCodeRandomPlacer(DeadPlacerContext& ctx) {
  Randomizer& rng = *ctx.rng;
  DeadPlacerWorld& world = *ctx.world;

  // ① 选数量 n ∈ [0,12]：拒绝采样（ja 无符号比较 > 12）
  int n = 0;
  do {
    n = Ftol(static_cast<double>(static_cast<std::uint64_t>(rng.Random())) *
                 kCountScale +
             kCountBias);
  } while (static_cast<unsigned>(n) > 12U);

  // ② 基准地形（thiscall 栈参坐标取格 +0x11B，movsx 有符号字节）
  const int base_level = world.Level(ctx.anchor);

  // ③ 阈值：lo = (this+0x38==0) ? 15 : 0；hi = 35（成功后递减）
  int lo = (ctx.this38 == 0) ? 15 : 0;
  int hi = 35;                       // [esp+0x18]，0x5A869A dec

  // ④ 容器收集 36 项表（ABAN01..CA0021 类型名）：
  //    0x5AD6F0 构造 + 虚表槽 8 = SetCapacity(计数+10) 扩容，收集全部表项。
  //    golden 桩 SetCapacity 恒成功 → 36 项全收。0x5A82E0 是零引用死代码，
  //    不回写任何全局容器，C++ 侧就是全量收集（不发明，只是把 pbuf 简化）。
  std::vector<std::string> pool;
  for (int i = 0; i < 36; ++i) {
    pool.push_back((ctx.type_names != nullptr && ctx.type_names->size() >= 36U)
                       ? (*ctx.type_names)[static_cast<std::size_t>(i)]
                       : kPlacerTypeNames[i]);
  }

  // ⑤ owner：FindIndexOfName("Neutral") -> FindByCountryIndex
  const int owner = world.FindNeutralOwner();

  // ⑥ 外层循环：attempt < 2n 且 placed < n
  int placed = 0;                    // [esp+0x1C]
  int attempt = 0;                   // [esp+0x20]
  while (placed < n && attempt < 2 * n) {
    // 区间大小 = hi - lo + 1；(double)区间存 [esp+0x60]、(double)lo 存
    // [esp+0x40]——每次外层循环重算（hi 会递减）
    const int range = hi - lo + 1;

    // 拒绝采样 idx ∈ [lo, hi]（cmp eax, hi; ja 重摇——无符号比较）
    int idx = 0;
    do {
      idx = Ftol(static_cast<double>(
                     static_cast<std::uint64_t>(rng.Random())) *
                     static_cast<double>(range) * kInvUintMax +
                 static_cast<double>(lo));
    } while (static_cast<unsigned>(idx) > static_cast<unsigned>(hi));

    // 候选 = 容器[idx]（类型名）→ 0x45E7B0 全局名字表 → 类型索引 或 -1
    const std::string& name = pool[static_cast<std::size_t>(idx)];
    const int type = world.LookupTypeIndex(name);
    if (type == -1) {
      // ⚠️ 原版怪癖（0x5A8489 je 0x5A86E1）：名字表查不到时**跳过
      // attempt++** 直接跑到外层尾部的 placed<n 判断——placed<n 恒成立时
      // 永不前进，死循环。零引用死代码从未被启用正因如此。黄金 golden 的
      // 用例全部避开此分支（名字表必命中），此处照抄跳转（continue 语义
      // = 回到外层头部，attempt 保持不变）。
      continue;
    }

    // operator new(0x720) + BuildingClass(0x43B740, [0xA83C6C][type], owner)
    int handle = -1;
    if (world.AllocBuildingMemory()) {
      handle = world.CreateBuilding(type, owner);
    }

    // 内层 tries < 60
    int tries = 0;                   // [esp+0x40]
    bool done = false;               // [esp+0x12]
    while (tries < 60) {
      // r1/r2 ∈ [0,14] 各自拒绝采样（*15/(2^32-1)，>14 重摇）
      int r1 = 0;
      do {
        r1 = Ftol(static_cast<double>(
                      static_cast<std::uint64_t>(rng.Random())) *
                  kOffsetScale);
      } while (static_cast<unsigned>(r1) > 14U);
      int r2 = 0;
      do {
        r2 = Ftol(static_cast<double>(
                      static_cast<std::uint64_t>(rng.Random())) *
                  kOffsetScale);
      } while (static_cast<unsigned>(r2) > 14U);

      // x' = 锚x + r2 - 7；y' = 锚y + r1 - 7（int16 回绕）
      const CellCoord nxt{
          static_cast<std::int16_t>(ctx.anchor.x + (r2 - 7)),
          static_cast<std::int16_t>(ctx.anchor.y + (r1 - 7))};

      // 占地表：虚表 +0x90(1) 返回 short 对列表，(0x7FFF,0x7FFF) 收尾
      const std::vector<PlacerFoundationOffset>& fnd =
          world.Foundation(handle);
      bool ok = true;                // [esp+0x13]
      for (std::size_t i = 0;; ++i) {
        const PlacerFoundationOffset& f = fnd[i];
        // 双分量都 == 0x7FFF 才算表尾
        if (f.dx == 0x7FFF && f.dy == 0x7FFF) {
          if (ok) {
            // 全过 → 落地：像素 (x'<<8)+0x80 交给 world.Unlimbo 换算（与
            // tech_buildings 同一桩语义：传 cell 坐标，桩内还原像素）
            if (world.Unlimbo(handle, nxt)) {
              ++placed;              // [esp+0x1C]
              done = true;           // [esp+0x12]
              // 容器前移压缩（删除 idx 处已用的类型名）
              if (idx < static_cast<int>(pool.size())) {
                pool.erase(pool.begin() + idx);
              }
              --hi;                  // 0x5A869A：上界递减
            }
          }
          break;
        }
        // 合成格 = 占地偏移 + (x', y')，int16 回绕（eax 32 位加只取低 16）
        const CellCoord n{static_cast<std::int16_t>(f.dx + nxt.x),
                          static_cast<std::int16_t>(f.dy + nxt.y)};
        // 校验链（与 tech_buildings 分支 B 同款五重）
        if (world.HasOccupier(n) || !world.IsPlain(n) ||
            world.Level(n) != base_level || world.Field0xEC(n) == 3 ||
            !world.WithinUsableArea(n)) {
          ok = false;                // [esp+0x13] = 0
          break;
        }
      }
      ++tries;
      if (done) break;
    }

    // tries >= 60 且没放下 → 虚表 +0x20(1) 销毁
    if (!done && handle >= 0) {
      world.Destroy(handle);
    }
    ++attempt;                       // [esp+0x20]
  }
}

}  // namespace rmg
}  // namespace ra2