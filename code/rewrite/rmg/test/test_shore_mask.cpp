// 八邻域「水」掩码 ShoreMask（原版 0x57B210）的黄金对拍
//
// 基准由 code/randmap/dump_shore_mask_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/shore_mask_golden.txt。
// 两张随机地图 × 全部格子 × 三种 mode = 1299 次查询，覆盖 122 个不同掩码值。
#include "shore_mask.h"

#include <map>
#include <vector>

#include "gtest/gtest.h"

namespace ra2 {
namespace rmg {
namespace {

// 测试用的气候数据：只有判水区间的基址有意义
const TheaterData& Theater() {
  static const TheaterData t = [] {
    TheaterData d;
    d.water_set = 0x100;
    return d;
  }();
  return t;
}

#include "shore_mask_golden.inc"

// 用一张 map 顶替引擎的格子数组；查不到的坐标 = 格子不存在。
class TableWorld : public ShoreWorld {
 public:
  struct Rec {
    int tile;
    bool valid;
    int cache;
  };

  int IsoTile(const CellCoord& c) const override {
    auto it = recs_.find(Key(c));
    return it == recs_.end() ? kNoCell : it->second.tile;
  }
  bool WorkValid(const CellCoord& c) const override {
    auto it = recs_.find(Key(c));
    return it != recs_.end() && it->second.valid;
  }
  int MaskCache(const CellCoord& c) const override {
    auto it = recs_.find(Key(c));
    return it == recs_.end() ? -1 : it->second.cache;
  }

  void Add(int x, int y, const Rec& r) { recs_[Key(CellCoord{x, y})] = r; }

 private:
  static long long Key(const CellCoord& c) {
    return static_cast<long long>(c.y) * 4096 + c.x;
  }
  std::map<long long, Rec> recs_;
};

void RunMap(int w, int h, const int (*cells)[5], int num_cells,
            const int (*masks)[4], int num_masks) {
  TableWorld world;
  for (int i = 0; i < num_cells; ++i) {
    world.Add(cells[i][0], cells[i][1],
              {cells[i][2], cells[i][3] != 0, cells[i][4]});
  }
  const MapGrid grid(w, h);
  for (int i = 0; i < num_masks; ++i) {
    const CellCoord c{masks[i][0], masks[i][1]};
    const int mode = masks[i][2];
    EXPECT_EQ(ShoreMask(world, grid, Theater(), c, mode), masks[i][3])
        << "map " << w << "x" << h << " cell (" << c.x << "," << c.y
        << ") mode " << mode;
  }
}

TEST(ShoreMask, MatchesOriginal) {
  RunMap(kMap0W, kMap0H, kMap0Cells,
         static_cast<int>(sizeof(kMap0Cells) / sizeof(kMap0Cells[0])),
         kMap0Masks,
         static_cast<int>(sizeof(kMap0Masks) / sizeof(kMap0Masks[0])));
  RunMap(kMap1W, kMap1H, kMap1Cells,
         static_cast<int>(sizeof(kMap1Cells) / sizeof(kMap1Cells[0])),
         kMap1Masks,
         static_cast<int>(sizeof(kMap1Masks) / sizeof(kMap1Masks[0])));
}

// 位定义本身单独钉一遍：bit k ⟺ AdjacentCell[(k+1)&7] 是水。
// 造一张只有一个邻格是水的图，看点亮的是不是预期那一位。
TEST(ShoreMask, BitLayoutIsCounterClockwiseFromNE) {
  struct { int dx, dy, bit; } kExpect[] = {
      {1, -1, kMaskNE}, {1, 0, kMaskE},  {1, 1, kMaskSE},  {0, 1, kMaskS},
      {-1, 1, kMaskSW}, {-1, 0, kMaskW}, {-1, -1, kMaskNW}, {0, -1, kMaskN},
  };
  const MapGrid grid(20, 20);
  const CellCoord c{20, 20};   // 菱形正中，八邻俱全
  for (const auto& e : kExpect) {
    TableWorld world;
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        const bool water = (dx == e.dx && dy == e.dy);
        world.Add(c.x + dx, c.y + dy, {water ? 0x100 : 0x50, true, -1});
      }
    }
    // 中心格自己不是水，所以出图的邻格（这里没有）也不会点亮位
    EXPECT_EQ(ShoreMask(world, grid, Theater(), c, 2), e.bit)
        << "dir (" << e.dx << "," << e.dy << ")";
  }
}

// 边界不产生假岸线：邻格出图时取**中心格自己**的 IsWater。
TEST(ShoreMask, MissingNeighbourInheritsCentre) {
  const MapGrid grid(20, 20);
  const CellCoord c{20, 20};
  {   // 中心是水 → 八邻全缺 → 掩码全 1
    TableWorld world;
    world.Add(c.x, c.y, {0x100, true, -1});
    EXPECT_EQ(ShoreMask(world, grid, Theater(), c, 2), 0xFF);
  }
  {   // 中心是陆 → 八邻全缺 → 掩码 0
    TableWorld world;
    world.Add(c.x, c.y, {0x50, true, -1});
    EXPECT_EQ(ShoreMask(world, grid, Theater(), c, 2), 0);
  }
}

// 缓存命中就直接返回，而且**不区分 mode**——这是原版的行为，必须照抄。
TEST(ShoreMask, CacheIsReturnedVerbatimAndIgnoresMode) {
  const MapGrid grid(20, 20);
  const CellCoord c{20, 20};
  TableWorld world;
  world.Add(c.x, c.y, {0x50, true, 0x5A});
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx || dy) world.Add(c.x + dx, c.y + dy, {0x100, true, -1});
    }
  }
  // 现算应当是 0xFF，但缓存里是 0x5A，三种 mode 都返回缓存
  EXPECT_EQ(ShoreMask(world, grid, Theater(), c, 1), 0x5A);
  EXPECT_EQ(ShoreMask(world, grid, Theater(), c, 2), 0x5A);
}

// mode 门槛：0 要求「尚未定砖」，1 要求「自己不是水」，2 无门槛。
TEST(ShoreMask, ModeGates) {
  const MapGrid grid(20, 20);
  const CellCoord c{20, 20};
  auto build = [&](int self_tile) {
    auto* w = new TableWorld();
    w->Add(c.x, c.y, {self_tile, true, -1});
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx)
        if (dx || dy) w->Add(c.x + dx, c.y + dy, {0x100, true, -1});
    return w;
  };
  {   // 自己是水：mode 0 被「未定砖」挡掉，mode 1 被「自己是水」挡掉
    std::unique_ptr<TableWorld> w(build(0x100));
    EXPECT_EQ(ShoreMask(*w, grid, Theater(), c, 0), 0);
    EXPECT_EQ(ShoreMask(*w, grid, Theater(), c, 1), 0);
    EXPECT_EQ(ShoreMask(*w, grid, Theater(), c, 2), 0xFF);
  }
  {   // 尚未定砖（0）：mode 0 放行
    std::unique_ptr<TableWorld> w(build(0));
    EXPECT_EQ(ShoreMask(*w, grid, Theater(), c, 0), 0xFF);
    EXPECT_EQ(ShoreMask(*w, grid, Theater(), c, 1), 0xFF);
  }
  {   // 已是陆地砖：mode 0 挡掉，mode 1 放行
    std::unique_ptr<TableWorld> w(build(0x50));
    EXPECT_EQ(ShoreMask(*w, grid, Theater(), c, 0), 0);
    EXPECT_EQ(ShoreMask(*w, grid, Theater(), c, 1), 0xFF);
  }
}

// 工作记录 +0x4A 无效 → 一律返回 0，早于任何邻域扫描。
TEST(ShoreMask, InvalidWorkRecordReturnsZero) {
  const MapGrid grid(20, 20);
  const CellCoord c{20, 20};
  TableWorld world;
  world.Add(c.x, c.y, {0x50, false, -1});
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
      if (dx || dy) world.Add(c.x + dx, c.y + dy, {0x100, true, -1});
  EXPECT_EQ(ShoreMask(world, grid, Theater(), c, 2), 0);
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
