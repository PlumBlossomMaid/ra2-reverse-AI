#include "shore_mask.h"

namespace ra2 {
namespace rmg {
namespace {

// 邻格在指针数组里的相对下标与它点亮的位。原版是一串指针加法
// （0x57B2F9 起 `p = cells + Y*512 + X - 0x201`，然后 +4/+4/+0x7F8/+8/+0x7F8/+4/+4），
// 展开成 (dx, dy) 就是下面这张表。顺序即原版的求值顺序。
struct MaskStep {
  int dx;
  int dy;
  int bit;
};
constexpr MaskStep kSteps[8] = {
    {-1, -1, kMaskNW},  // p[0]
    {0, -1, kMaskN},    // p[1]
    {1, -1, kMaskNE},   // p[2]
    {-1, 0, kMaskW},    // p[0x200]
    {1, 0, kMaskE},     // p[0x202]
    {-1, 1, kMaskSW},   // p[0x400]
    {0, 1, kMaskS},     // p[0x401]
    {1, 1, kMaskSE},    // p[0x402]
};

}  // namespace

int ShoreMask(const ShoreWorld& world, const MapGrid& grid,
              const TheaterData& theater, const CellCoord& c, int mode) {
  const int water = theater.water_set;
  const int self_tile = world.IsoTile(c);

  // 0x57B23D：中心格自己是不是水，**在 mode 门槛之前就算好了**。
  // 出图的邻格会拿这个值顶上，所以地图边缘不会被判成岸。
  const bool self_is_water =
      self_tile != ShoreWorld::kNoCell && IsWaterTile(self_tile, water);

  // 0x57B242-0x57B271：菱形四不等式，出界返回 0。
  if (!grid.InMap(c)) return 0;

  // 0x57B27F-0x57B2B9：三种 mode 只差这道前置门槛。
  if (mode == 0) {
    if (!IsUndecided(self_tile)) return 0;
  } else if (mode == 1) {
    if (self_is_water) return 0;
  }

  // 0x57B2BC-0x57B2DC：工作记录必须有效。
  if (!world.WorkValid(c)) return 0;

  // 0x57B2DF：缓存命中就直接返回，**而且缓存不区分 mode**。
  // 现算的结果也**不写回**——写回由调用方自己做（0x57A430 那对相邻调用
  // 就是靠这个让 mode 0 的结果恒等于刚写进去的 mode 2 结果）。
  const int cached = world.MaskCache(c);
  if (cached >= 0) return cached;

  int mask = 0;
  for (const MaskStep& s : kSteps) {
    const CellCoord n{c.x + s.dx, c.y + s.dy};
    const int tile = world.IsoTile(n);
    const bool bit = (tile != ShoreWorld::kNoCell) ? IsWaterTile(tile, water)
                                                   : self_is_water;
    if (bit) mask |= s.bit;
  }
  return mask;
}

}  // namespace rmg
}  // namespace ra2
