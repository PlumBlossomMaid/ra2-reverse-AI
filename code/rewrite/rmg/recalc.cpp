#include "recalc.h"

#include <cstddef>
#include <vector>

#include "cell_ref.h"
#include "regions.h"        // IsGreenTile（0x4867B0）
#include "scaled_random.h"  // PickBelowMul（0x59B828 的内联展开）
#include "shore_mask.h"     // IsUndecided（0x486380）

namespace ra2 {
namespace rmg {

namespace {

// 0x59B78F / 0x59B8A3：两处都是 `for (edi = 0; edi < 8; edi += 2)`，
// 也就是只看四个**正**方向（N / E / S / W），对角一概不看。
const int kOrtho[4] = {0, 2, 4, 6};

void WriteTile(SmoothWorld& w, const CellRef& r, int tile) {
  if (r.dummy) {
    // 0x59B8A0 `mov [ebp+0x38], eax` —— ebp 有可能就是共享哑元 0xABDC50。
    // 只写 +0x38，不碰 +0x11A/+0x11B，与 SetDummyTile 的三写版语义不同。
    static_cast<TileWorld*>(&w)->SetDummyIsoTile(tile);
  } else {
    static_cast<TileWorld*>(&w)->SetIsoTile(r.coord, tile);
  }
}

// 0x544C20(subtile, flag)：拿到 .tmp 的图像头，`n = w * h`，
// `subtile >= n` 或偏移表该项为 0（空洞）都判假。
// flag 只控制「.tmp 还没装载就现装」，装载完之后两个值相同（见 recalc.h）。
bool SubTilePresent(const TileType* tt, int subtile) {
  // vtable+0x9C 返回 0（.tmp 没装载）时判假。砖表里那一格是**空指针**的话
  // 原版会当场解引用崩掉，这里按「判假」处理——照抄不到的一类。
  if (tt == nullptr) return false;
  const int n = tt->width * tt->height;
  if (subtile >= n) return false;
  if (subtile < 0 || static_cast<std::size_t>(subtile) >= tt->cells.size()) {
    return false;
  }
  return tt->cells[static_cast<std::size_t>(subtile)].present;
}

// 0x5471B0(subtile)：`idx = subtile % (w * h)`（`idiv` 取余，**不是**边界判定），
// 偏移表该项为 0 时返回 0，否则返回子块头 +0x2A 的**有符号字节**。
int RampOf(const TileType* tt, int subtile) {
  if (tt == nullptr) return 0;
  const int n = tt->width * tt->height;
  if (n == 0) return 0;
  const int idx = subtile % n;
  if (idx < 0 || static_cast<std::size_t>(idx) >= tt->cells.size()) return 0;
  const TileSubCell& sc = tt->cells[static_cast<std::size_t>(idx)];
  return sc.present ? sc.ramp : 0;
}

}  // namespace

void SpreadGreen(SmoothWorld& world, const MapGrid& grid,
                 const TheaterData& theater, Randomizer& rng) {
  // 局部 `DynamicVectorClass<CellClass*>`：构造 0x5AD820(0, 0)（容量 0、
  // 数据 0、IsAllocated = 0），随后 0x59B759-0x59B765 把虚表改成 0x7ED9BC、
  // 个数清 0、增长步长写成 **10000**。
  //
  // 内联的 AddItem（0x59B7A4-0x59B7E6）是：
  //   count < capacity            → 直接写
  //   否则要 (IsAllocated || capacity == 0) 且 growth > 0，再 Resize(cap+growth)
  // 首次进来 capacity == 0 过关，Resize（0x5AD010）之后 IsAllocated 被置 1，
  // 于是**之后每次都过关**；Resize 只在 malloc 失败时返回 0。
  // 结论：整条 AddItem 等价于一次 push_back，容量机器不可观测，不建模。
  std::vector<CellRef> front;

  // ── 第一遍 0x59B76D-0x59B7FD：扫全图（MapClass 迭代器序）──
  // 中心格必须是绿地（0x4867B0：== GreenTile 或落在 ClearToGreenLat 的 16 块里），
  // 四个正邻里凡是「还没定砖」的就收进前沿。
  //
  // ⚠️ 没有去重：同一格可以被好几个绿地邻居重复收进来，也可以在被涂绿之后
  // 再次作为别人的邻居入队。
  // ⚠️ 出图的邻格会退化成共享哑元 0xABDC50——而哑元刚开局的 +0x38 是 0，
  // 也就是「还没定砖」，于是它会被真的收进前沿，还可能被抽中涂绿。
  grid.ForEachCell([&](const CellCoord& c) {
    if (!IsGreenTile(world.Tile(c), theater)) return;
    for (const int dir : kOrtho) {
      const CellRef n = Adjacent(world, RefOf(c), dir);
      if (IsUndecided(TileOf(world, n))) front.push_back(n);
    }
  });

  // ── 轮数 0x59B7FF-0x59B81E ──
  // `imul 0x55555556` + `edx + (edx >>> 31)` 就是编译器实现的「有符号除 3、
  // 向零截断」。⚠️ 第一遍扫完之后**一次算定**，后面前沿再怎么涨都不改。
  int rounds = static_cast<int>(front.size()) / 3;
  if (rounds > 1000) rounds = 1000;

  // 顺带一条能救命的算术事实：每轮只删 1 个，而 rounds <= count/3，
  // 所以前沿永远不会被抽空——否则 0x59B830 的 `hi = count - 1` 会变成 -1，
  // `ja` 恒假、第一次抽就被接受，然后越界读 front[k]。
  for (; rounds > 0; --rounds) {
    // 唯一的取数点 0x59B842。span 每轮重算（0x59B828 在循环体内）。
    const int n = static_cast<int>(front.size());
    const int pick = PickBelowMul(rng, n);

    // ⚠️ 先取元素（0x59B875 `mov ebp,[ecx+eax*4]`），**再**删。
    const CellRef chosen = front[static_cast<std::size_t>(pick)];
    // 0x59B885-0x59B897 是**保序前移**，不是「拿末尾顶上」。
    front.erase(front.begin() + pick);

    // 0x59B899：涂成 GreenTile。只写 +0x38，子块与层高一概不动。
    WriteTile(world, chosen, theater.green_tile);

    // 0x59B8A3-0x59B901：新绿地的四个正邻继续入队。
    for (const int dir : kOrtho) {
      const CellRef nb = Adjacent(world, chosen, dir);
      if (IsUndecided(TileOf(world, nb))) front.push_back(nb);
    }
  }
}

void RecalcAttributes(SmoothWorld& world, const CellCoord& c, int tile_count) {
  TileWorld& tw = *static_cast<TileWorld*>(&world);

  // 0x47D57B-0x47D58D：砖号越界就整格作废。有符号比较。
  int tile = world.Tile(c);
  if (tile >= tile_count) {
    tile = 0xFFFF;
    tw.SetIsoTile(c, tile);
  }
  // 0x47D595：0xFFFF 跳去 0x47DB22，那条分支不碰砖号/子块/层高。
  if (tile == 0xFFFF) return;

  const TileType* tt = world.TileByIndex(tile);
  const int subtile = tw.SubTile(c);
  if (!SubTilePresent(tt, subtile)) {
    // 0x47D5E6-0x47D5F9：砖号作废、子块清 0、坡道清 0（+0xEC 也清 0，
    // 那是「地面类型」缓存，与地形输出无关）。
    tw.SetIsoTile(c, 0xFFFF);
    tw.SetSubTile(c, 0);
    tw.SetRamp(c, 0);
    return;
  }
  // 0x47D7FB-0x47D80D：合格的话只把坡道类型重算一遍。
  tw.SetRamp(c, RampOf(tt, subtile));
}

void RecalcAttributesPass(SmoothWorld& world, const MapGrid& grid,
                          int tile_count) {
  grid.ForEachCell(
      [&](const CellCoord& c) { RecalcAttributes(world, c, tile_count); });
}

}  // namespace rmg
}  // namespace ra2
