// 岸线平滑 `FUN_0057A0C0`——四遍扫全图，把水陆边界修成能贴砖的形状。
//
// 涂块（`0x59BBC0`）每涂完一块陆地就调它一次。它先侵蚀掉贴不出砖的窄陆桥，
// 再清掉几种病态水格，最后两遍才真的把岸砖压上去。任何一遍中途判失败，
// 整块陆地会被调用方回滚——所以它的返回值直接决定地图长什么样。
//
// 它也是**阶段 2 里唯一还在消耗随机数**的地方：第 3/4 遍每格无条件抽一次
// `PickRange(0, 5)`。抽取次数错一次，后面所有阶段的随机流就全错位。
//
// 取证（gamemd.exe）：
//   编排 0x57A0C0-0x57A310（四遍之间重置迭代器全局 0x87F8F4/F8/FC/900）
//   第 1 遍 侵蚀 0x57A430-0x57A8F1
//   第 2 遍 清理 0x57A320-0x57A425
//   第 3/4 遍 直边/拐角 0x57ACF0-0x57B205
//   邻格 0x481810 + 指针表查表 0x5657A0，方向表 0x89F688（运行期由 0x49F2F0 填）
//   判水 0x485060，工作记录 0x58C2A0/0x58C2C0，块归属 0x5A00C0/0x5A0090
// 详见 docs/random-map/stage-seeding-water.md §七。
#ifndef RA2_RMG_SHORE_SMOOTH_H_
#define RA2_RMG_SHORE_SMOOTH_H_

#include "map_grid.h"
#include "place_tile.h"
#include "randomizer.h"
#include "shore_mask.h"
#include "theater.h"

namespace ra2 {
namespace rmg {

// `[0xABDB64]` 落点偏移表，索引 = 组内下标 i ∈ [0, 0x2A]。
// 由 CRT 静态初始化器 0x57A9C0 填出，与 theater 无关（模拟执行 dump 所得，
// 与 memory/data/rmg/shore_tables.txt 一致）。
extern const short kShoreOffsetX[43];
extern const short kShoreOffsetY[43];

// 平滑要碰的全部外部状态。掩码那份依赖（ShoreWorld）和贴砖那份（TileWorld）
// 都要，所以直接继承两者。
//
// ⚠️ 两个基类各自声明了同名同签名的纯虚 `IsoTile`。实现方**写一份**即可同时
// 覆盖两者（C++ 允许一个成员函数覆盖多个基类虚函数），但调用点必须限定作用域，
// 统一走下面这个非虚壳子 `Tile()`。
class SmoothWorld : public ShoreWorld, public TileWorld {
 public:
  // ⚠️ 必须先转成基类指针再调，不能写 `ShoreWorld::IsoTile(c)`——那是限定名调用，
  // 会退化成非虚静态派发，直接链到纯虚函数上去（MSVC 报 LNK2019）。
  int Tile(const CellCoord& c) const {
    return static_cast<const ShoreWorld*>(this)->IsoTile(c);
  }

  // 工作记录 +0x40 掩码缓存的写入（读在 ShoreWorld::MaskCache）
  virtual void SetMaskCache(const CellCoord& c, int v) = 0;

  // 0x57A12B-0x57A14A：进场时把 side² 条记录的 +0x40 全部置 -1。
  // ⚠️ 是**整个数组**，不止菱形里那些格。
  virtual void ResetMaskCaches() = 0;

  // CellClass +0x11B，第 3/4 遍拿它当贴砖的基准高度（`movsx` 成有符号字节）
  virtual int Level(const CellCoord& c) const = 0;

  // 共享哑元格子 0xABDC50 当前的坐标（+0x24，即 0xABDC74）。
  // 走线时两条线可能同时退化成哑元，此后它们的坐标是**同一份**——
  // 这个怪癖必须能被复现，所以要读得到。
  virtual CellCoord DummyCoord() const = 0;

  // `[0xA8ED2C]` 全局砖表；越界或该砖未装载时返回 nullptr。
  virtual const TileType* TileByIndex(int index) const = 0;
};

// `MapClass::Shoreline(blob_id, flag)`（0x57A0C0，`ret 8`，`this = 0x87F7E8`）。
//   blob_id  当前陆块编号（涂块的 MapSeedClass +0x308）
//   flag     为真时：撞到别的块不判失败、块归属改写成 0、并且一路传进
//            贴砖器的 no_blob_tracking。涂块收尾（0x59C433）传 1，
//            阶段 2 调度收尾（0x59A788）传 0，别假定它恒为某值。
// 返回 false = 这块地铺不出岸线，调用方要整块回滚。
//
// ⚠️ 必须在 FpuChopScope 作用域内调用（第 3/4 遍的 PickRange 走 x87）。
bool ShorelineSmooth(SmoothWorld& world, const MapGrid& grid,
                     const TheaterData& theater, Randomizer& rng,
                     int blob_id, bool flag);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_SHORE_SMOOTH_H_
