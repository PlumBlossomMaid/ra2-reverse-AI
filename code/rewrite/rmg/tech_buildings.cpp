#include "tech_buildings.h"

#include <cstdint>

namespace ra2 {
namespace rmg {

void AddTechBuildings(TechContext& ctx) {
  TechWorld& world = *ctx.world;
  std::vector<PaintWorkCell>& work = *ctx.work;

  if (ctx.map_type == 2) {
    // —— 分支 A：给每个有出生点的区域各补几栋 ——
    // 0x5A95BF：折叠常数 [0x7ED8C0] = 3/(2^32-1)，拒绝 > 2
    const int rounds = PickBelow(*ctx.rng, 3);
    if (rounds <= 0) return;       // 0x5A95E4 jle：一栋不放，但随机数已经花了
    int n = rounds;
    do {
      // ⚠️ 区域表与个数在每次 0x595400 之后都**重新从全局读**
      // （0x5A9615 / 0x5A961B）——放建筑可能会动那个向量
      for (int i = 0; i < ctx.regions->Count(); ++i) {
        RegionClass* r = ctx.regions->At(i);
        if (r->start_points > 0) world.PlaceRandomBuilding(r);
      }
      --n;
    } while (n != 0);
    return;                        // 这条分支到此为止，不走分支 B
  }

  // —— 分支 B：全图随机撒 ——
  // 找业主：HouseTypeClass::FindIndexByName("Neutral") -> FindHouseByType。
  // 两次调用的结果只喂给 BuildingClass 的构造函数，算法本身不读，
  // 所以放在 TechWorld::CreateBuilding 里，这里不再复述。

  // 0x5A9645：折叠常数 [0x7EDAB0] = 5/(2^32-1)，拒绝 > 4
  int m = PickBelow(*ctx.rng, 5);
  if (m <= 0) return;              // m == 0 一栋都不放，随机数照花
  do {
    bool placed = false;           // [esp+0x13]
    int attempt = 0;               // [esp+0x1C]
    int handle = -1;               // edi / [esp+0x18]
    if (world.AllocBuildingMemory()) {
      // 0x5A96A2 起：count 先 `dec` 成上界，再 `lea ecx,[esi+1]` 把它加回来
      // 当跨度——两次乘法的那一路（PickBelowMul），不是折叠常数那路
      const int count = world.NeutralTechBuildingCount();
      const int k = PickBelowMul(*ctx.rng, count);
      handle = world.CreateBuilding(k);
    }

    for (;;) {
      if (attempt >= 100) {        // 0x5A9713 cmp / jge
        // 0x5A98EA：没放下去就调虚函数 +0x20(1) 把对象删掉
        if (!placed && handle >= 0) world.Destroy(handle);
        break;
      }

      // —— 摸一个落点 ——
      int tries = 0;               // ebp，**每个 attempt 都重置**（0x5A9711）
      CellCoord at;
      for (;;) {
        int idx;
        do {
          // 0x5A971E：side² - 1 每轮重算（值不变，无副作用）
          idx = PickRange(*ctx.rng, 0, ctx.side * ctx.side - 1);
          // ⚠️ 0x5A9757 jne 跳回的是**取数点之前**：工作记录里坐标为 (0,0)
          // 的槽位（菱形外，阶段 1 没填）被拒绝时 tries 不加，随机数照花
        } while (work[static_cast<std::size_t>(idx)].coord == CellCoord{0, 0});
        ++tries;
        at = work[static_cast<std::size_t>(idx)].coord;
        if (tries > 200) {
          // 0x5A9791：不是放弃，而是**强行用格 (0,0)**。后面的占地检查基本
          // 会失败，于是白耗一次 attempt——随机数也照样花了
          at = CellCoord{0, 0};
          break;
        }
        if (world.IsPlain(at)) break;                 // 0x5A9786
      }

      // —— 占地检查 ——
      // 0x5A97C1：锚点格的层高，movsx 有符号字节
      const int base_level = world.Level(at);
      const std::vector<FoundationOffset>& fnd = world.Foundation(handle);
      bool ok = true;                                 // bl
      for (std::size_t i = 0;; ++i) {
        const FoundationOffset& f = fnd[i];
        // 0x5A97D7：**两个分量都是 0x7FFF** 才算表尾
        if (f.dx == 0x7FFF && f.dy == 0x7FFF) {
          if (ok) {
            // 0x5A9889：coord = (x*256+128, y*256+128, 0)，x/y 按 int16 取符号
            if (world.Unlimbo(handle, at)) placed = true;
          }
          break;
        }
        // 0x5A97E7 起：占地偏移与锚点按 **int16** 相加
        const CellCoord n{static_cast<std::int16_t>(f.dx + at.x),
                          static_cast<std::int16_t>(f.dy + at.y)};
        // 0x5A9812 GetCellAt(n)：出图就是共享哑元，读到的是上一次的残留。
        // 随后 0x5A9853 那个工作记录下标读的是**格子自己的 +0x24/+0x26**，
        // 而 GetCellAt 刚把请求坐标写进去了，所以与 n 完全相同。
        if (world.HasOccupier(n) ||                   // 0x5A9819 +0xE4 != 0
            !world.IsPlain(n) ||                      // 0x5A9825
            world.Level(n) != base_level ||           // 0x5A982E
            world.Field0xEC(n) == 3 ||                // 0x5A9839
            !world.WithinUsableArea(n) ||             // 0x5A984A
            work[static_cast<std::size_t>(n.y * ctx.side + n.x)].flag45) {
          ok = false;                                 // 0x5A9877 xor bl,bl
        }
        if (!ok) break;                               // 0x5A987E
      }

      ++attempt;                                      // 0x5A98D9
      if (placed) break;
    }
    --m;
  } while (m != 0);
}

}  // namespace rmg
}  // namespace ra2
