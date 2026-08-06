// 等距地图几何：格子集合、遍历顺序、总格数
//
// 走位期望值来自 Unicorn 直接跑原版 0x578350/0x578290，
// 存档 memory/data/rmg/map_iterator_golden.txt。
#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "map_grid.h"
#include "map_size.h"
#include "scaled_random.h"

namespace {

using ra2::rmg::CellCoord;
using ra2::rmg::MapGrid;

struct WalkCase {
  int w;
  CellCoord seq[32];
};

// 原版迭代器的前 32 步（不含菱形停机，纯走位）
const WalkCase kWalk[] = {
    {3, {{1,3}, {2,2}, {3,1}, {2,3}, {3,2}, {2,4}, {3,3}, {4,2}, {3,4}, {4,3}, {3,5}, {4,4}, {5,3}, {4,5}, {5,4}, {4,6}, {5,5}, {6,4}, {5,6}, {6,5}, {5,7}, {6,6}, {7,5}, {6,7}, {7,6}, {6,8}, {7,7}, {8,6}, {7,8}, {8,7}, {7,9}, {8,8}}},
    {4, {{1,4}, {2,3}, {3,2}, {4,1}, {2,4}, {3,3}, {4,2}, {2,5}, {3,4}, {4,3}, {5,2}, {3,5}, {4,4}, {5,3}, {3,6}, {4,5}, {5,4}, {6,3}, {4,6}, {5,5}, {6,4}, {4,7}, {5,6}, {6,5}, {7,4}, {5,7}, {6,6}, {7,5}, {5,8}, {6,7}, {7,6}, {8,5}}},
    {5, {{1,5}, {2,4}, {3,3}, {4,2}, {5,1}, {2,5}, {3,4}, {4,3}, {5,2}, {2,6}, {3,5}, {4,4}, {5,3}, {6,2}, {3,6}, {4,5}, {5,4}, {6,3}, {3,7}, {4,6}, {5,5}, {6,4}, {7,3}, {4,7}, {5,6}, {6,5}, {7,4}, {4,8}, {5,7}, {6,6}, {7,5}, {8,4}}},
    {6, {{1,6}, {2,5}, {3,4}, {4,3}, {5,2}, {6,1}, {2,6}, {3,5}, {4,4}, {5,3}, {6,2}, {2,7}, {3,6}, {4,5}, {5,4}, {6,3}, {7,2}, {3,7}, {4,6}, {5,5}, {6,4}, {7,3}, {3,8}, {4,7}, {5,6}, {6,5}, {7,4}, {8,3}, {4,8}, {5,7}, {6,6}, {7,5}}},
    {7, {{1,7}, {2,6}, {3,5}, {4,4}, {5,3}, {6,2}, {7,1}, {2,7}, {3,6}, {4,5}, {5,4}, {6,3}, {7,2}, {2,8}, {3,7}, {4,6}, {5,5}, {6,4}, {7,3}, {8,2}, {3,8}, {4,7}, {5,6}, {6,5}, {7,4}, {8,3}, {3,9}, {4,8}, {5,7}, {6,6}, {7,5}, {8,4}}},
    {8, {{1,8}, {2,7}, {3,6}, {4,5}, {5,4}, {6,3}, {7,2}, {8,1}, {2,8}, {3,7}, {4,6}, {5,5}, {6,4}, {7,3}, {8,2}, {2,9}, {3,8}, {4,7}, {5,6}, {6,5}, {7,4}, {8,3}, {9,2}, {3,9}, {4,8}, {5,7}, {6,6}, {7,5}, {8,4}, {9,3}, {3,10}, {4,9}}},
    {70, {{1,70}, {2,69}, {3,68}, {4,67}, {5,66}, {6,65}, {7,64}, {8,63}, {9,62}, {10,61}, {11,60}, {12,59}, {13,58}, {14,57}, {15,56}, {16,55}, {17,54}, {18,53}, {19,52}, {20,51}, {21,50}, {22,49}, {23,48}, {24,47}, {25,46}, {26,45}, {27,44}, {28,43}, {29,42}, {30,41}, {31,40}, {32,39}}},
    {120, {{1,120}, {2,119}, {3,118}, {4,117}, {5,116}, {6,115}, {7,114}, {8,113}, {9,112}, {10,111}, {11,110}, {12,109}, {13,108}, {14,107}, {15,106}, {16,105}, {17,104}, {18,103}, {19,102}, {20,101}, {21,100}, {22,99}, {23,98}, {24,97}, {25,96}, {26,95}, {27,94}, {28,93}, {29,92}, {30,91}, {31,90}, {32,89}}},
};

TEST(MapGeometry, WalkMatchesOriginal) {
  for (const WalkCase& c : kWalk) {
    // H 取足够大，保证前 32 步都还在菱形内
    MapGrid g(c.w, 1000);
    std::vector<CellCoord> got;
    g.ForEachCell([&](const CellCoord& p) {
      if (got.size() < 32) got.push_back(p);
    });
    ASSERT_GE(got.size(), 32u) << "W=" << c.w;
    for (int i = 0; i < 32; ++i) {
      EXPECT_EQ(got[i].x, c.seq[i].x) << "W=" << c.w << " step " << i;
      EXPECT_EQ(got[i].y, c.seq[i].y) << "W=" << c.w << " step " << i;
    }
  }
}

// 遍历到的格子集合 == 菱形判定的集合，且不重不漏。
// 这两者一个来自迭代器（0x578290），一个来自边界判定（0x57A3C6 / 0x57B254），
// 是两条独立的证据链，相等本身就是最强的交叉验证。
TEST(MapGeometry, IteratedSetEqualsDiamondPredicate) {
  for (int w = 3; w <= 40; ++w) {
    for (int h = 1; h <= 40; ++h) {
      MapGrid g(w, h);
      std::vector<CellCoord> seq;
      g.ForEachCell([&](const CellCoord& p) { seq.push_back(p); });
      std::set<std::pair<int, int>> got;
      for (const CellCoord& p : seq) got.insert({p.x, p.y});
      ASSERT_EQ(got.size(), seq.size()) << "W=" << w << " H=" << h << " 有重复";

      std::set<std::pair<int, int>> want;
      for (int x = 0; x <= 3 * (w + h); ++x)
        for (int y = 0; y <= 3 * (w + h); ++y)
          if (g.InMap(x, y)) want.insert({x, y});
      EXPECT_EQ(got, want) << "W=" << w << " H=" << h;
    }
  }
}

// 总格数 = H * (2W - 1)
TEST(MapGeometry, CellCountFormula) {
  for (int w = 3; w <= 60; ++w) {
    for (int h = 1; h <= 60; ++h) {
      MapGrid g(w, h);
      int n = 0;
      g.ForEachCell([&](const CellCoord&) { ++n; });
      ASSERT_EQ(n, g.CellCount()) << "W=" << w << " H=" << h;
      ASSERT_EQ(n, h * (2 * w - 1));
    }
  }
}

// 打包坐标：低 16 位 X、高 16 位 Y（工作记录 +0x00 与掩码函数取法一致）
TEST(MapGeometry, PackedCoord) {
  CellCoord c{0x1234, 0x5678};
  EXPECT_EQ(c.Packed(), 0x56781234u);
  EXPECT_TRUE(CellCoord::FromPacked(0x56781234u) == c);
}

// 实际尺寸下的规模：2 人默认图 70x70
TEST(MapGeometry, DefaultTwoPlayerMap) {
  ra2::rmg::FpuChopScope chop;
  ra2::rmg::MapSize s = ra2::rmg::ComputeMapSize(0, 0, 2, 1);
  MapGrid g(s.width, s.height);
  EXPECT_EQ(g.side(), 141);
  EXPECT_EQ(g.CellCount(), 70 * 139);
  int n = 0;
  g.ForEachCell([&](const CellCoord&) { ++n; });
  EXPECT_EQ(n, 9730);
}

}  // namespace
