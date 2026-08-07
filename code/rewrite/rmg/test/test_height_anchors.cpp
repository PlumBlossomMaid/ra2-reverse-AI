// 阶段 9 第 ① 步「打锚点」（原版 0x5A33F0）的黄金对拍
//
// 基准由 code/randmap/dump_height_anchors_golden.py 用 Unicorn 跑原版机器码
// 取得，存档 memory/data/rmg/height_anchors_golden.txt。
// 每个用例给一张「菱形内每格的 (砖号, 子格号)」初态，跑完之后比：
//   1) 工作数组里**每一个**被改动的槽位（下标、height 的 8 字节位型、+0x45）
//   2) 没被基准列出的槽位必须原样不动
//
// 位型而不是十进制：这一遍只写常量 0.5，收位型能顺手把
// 「立即数 0x3FE0000000000000」这条钉死；悬崖那支写 lock 不写 height，
// 位型全零正是它的指纹。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "height_field.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

// temperate 一列（memory/data/rmg/tileset_starts.txt），与基准脚本的
// THEATER 表逐项一致
TheaterData Temperate() {
  TheaterData t;
  t.water_set = 314;
  t.shore_pieces = 89;
  t.waterfall_east = 539;
  t.waterfall_west = 547;
  t.waterfall_north = 543;
  t.waterfall_south = 414;
  t.water_cliffs = 148;
  t.ramp_base = 29;
  t.bridge_set = 277;
  t.cliff_set = 39;
  t.cliff_ramps = 601;
  t.ramp_smooth = 621;
  t.water_caves = -1;
  t.wood_bridge_set = -1;
  t.destroyable_cliffs = 761;
  return t;
}

// 只读砖号/子格号的最小格子模型；其余接口这一遍碰不到，留空即可。
class AnchorStub : public TileWorld {
 public:
  bool CellExists(const CellCoord& c) const override {
    return cells_.count(Key(c)) != 0;
  }
  int IsoTile(const CellCoord& c) const override {
    std::map<long long, std::pair<int, int> >::const_iterator it =
        cells_.find(Key(c));
    return it == cells_.end() ? 0 : it->second.first;
  }
  int SubTile(const CellCoord& c) const override {
    std::map<long long, std::pair<int, int> >::const_iterator it =
        cells_.find(Key(c));
    return it == cells_.end() ? 0 : it->second.second;
  }
  void SetIsoTile(const CellCoord&, int) override {}
  void SetSubTile(const CellCoord&, int) override {}
  void SetLevel(const CellCoord&, int) override {}
  void SetRamp(const CellCoord&, int) override {}
  void SetDummyCoord(const CellCoord&) override {}
  int DummyIsoTile() const override { return 0; }
  int DummySubTile() const override { return 0; }
  void SetDummyTile(int, int, int) override {}
  void SetDummyIsoTile(int) override {}
  int WorkBlob(const CellCoord&) const override { return 0; }
  void SetWorkBlob(const CellCoord&, int) override {}

  void Preset(int x, int y, int tile, int subtile) {
    cells_[Key(CellCoord{x, y})] = std::make_pair(tile, subtile);
  }

 private:
  static long long Key(const CellCoord& c) { return c.y * 100000LL + c.x; }
  std::map<long long, std::pair<int, int> > cells_;
};

struct WorkDiff {
  std::string bits;   // 16 位十六进制，高字节在前
  int lock = 0;
};

struct Case {
  std::string name;
  int w = 0, h = 0;
  std::vector<int> tx, ty, tile, sub;
  std::map<int, WorkDiff> work;
};

std::string HeightBits(double v) {
  unsigned char raw[8];
  std::memcpy(raw, &v, 8);
  char buf[24];
  std::snprintf(buf, sizeof buf, "%02X%02X%02X%02X%02X%02X%02X%02X", raw[7],
                raw[6], raw[5], raw[4], raw[3], raw[2], raw[1], raw[0]);
  return std::string(buf);
}

std::vector<Case> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/height_anchors_golden.txt";
  std::ifstream f(path.c_str());
  EXPECT_TRUE(f.good()) << "打不开基准档 " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream is(line);
    std::string key;
    is >> key;
    if (key == "CASE") {
      Case c;
      is >> c.name >> c.w >> c.h;
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "TILE") {
      int x, y, t, st;
      is >> x >> y >> t >> st;
      c.tx.push_back(x);
      c.ty.push_back(y);
      c.tile.push_back(t);
      c.sub.push_back(st);
    } else if (key == "WORK") {
      int i, lk;
      std::string bits;
      is >> i >> bits >> lk;
      WorkDiff d;
      d.bits = bits;
      d.lock = lk;
      c.work[i] = d;
    }
  }
  return cases;
}

TEST(HeightAnchors, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name + " " + std::to_string(c.w) + "x" +
                 std::to_string(c.h));
    MapGrid grid(c.w, c.h);
    AnchorStub world;
    for (std::size_t k = 0; k < c.tile.size(); ++k) {
      world.Preset(c.tx[k], c.ty[k], c.tile[k], c.sub[k]);
    }
    // 基准里的砖号是 16 位（0xFFFF 就是 0xFFFF，不是 -1）——原版 CellClass
    // +0x38 是 dword，未定砖的两个取值 0 与 0xFFFF 都是正数
    const int n = grid.side() * grid.side();
    std::vector<PaintWorkCell> work(static_cast<std::size_t>(n));
    grid.ForEachCell([&](const CellCoord& p) {
      work[static_cast<std::size_t>(grid.WorkIndex(p))].coord = p;
    });

    MarkHeightAnchors(world, grid, theater, work);

    for (int i = 0; i < n; ++i) {
      const PaintWorkCell& w = work[static_cast<std::size_t>(i)];
      std::map<int, WorkDiff>::const_iterator it = c.work.find(i);
      if (it == c.work.end()) {
        ASSERT_EQ("0000000000000000", HeightBits(w.height))
            << "第 " << i << " 号槽位不该被写 height";
        ASSERT_FALSE(w.flag45) << "第 " << i << " 号槽位不该被打锚点";
      } else {
        ASSERT_EQ(it->second.bits, HeightBits(w.height))
            << "第 " << i << " 号槽位的 height 位型不对";
        ASSERT_EQ(it->second.lock != 0, w.flag45)
            << "第 " << i << " 号槽位的锚点标记不对";
      }
    }
  }
}

// 岸砖那支给 0.5、悬崖那支只打标记——两条分支的分水岭，单独钉一遍。
// 邻域是全 8 向且命中第一个未定砖就 break，所以正北（0 号方向）优先。
TEST(HeightAnchors, ShoreGivesHalfCliffOnlyLocks) {
  const TheaterData t = Temperate();
  MapGrid grid(8, 8);
  const int n = grid.side() * grid.side();

  for (int pass = 0; pass < 2; ++pass) {
    AnchorStub world;
    grid.ForEachCell([&](const CellCoord& c) { world.Preset(c.x, c.y, 0, 0); });
    const CellCoord seed{9, 9};
    ASSERT_TRUE(grid.InMap(seed));
    world.Preset(seed.x, seed.y, pass == 0 ? t.shore_pieces : t.cliff_set, 0);

    std::vector<PaintWorkCell> work(static_cast<std::size_t>(n));
    MarkHeightAnchors(world, grid, t, work);

    // 0 号方向是 (0,-1)：正北那一格拿到锚点，其余一格都没动
    int touched = 0;
    for (int i = 0; i < n; ++i) {
      if (work[static_cast<std::size_t>(i)].flag45) ++touched;
    }
    EXPECT_EQ(1, touched);
    const int idx = grid.WorkIndex(CellCoord{seed.x, seed.y - 1});
    EXPECT_TRUE(work[static_cast<std::size_t>(idx)].flag45);
    EXPECT_EQ(pass == 0 ? 0.5 : 0.0, work[static_cast<std::size_t>(idx)].height);
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
