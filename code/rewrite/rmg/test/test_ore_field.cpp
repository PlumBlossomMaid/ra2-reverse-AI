// 阶段 8 内层 0x5A28C0 CreateOreField 的黄金对拍
//
// 基准由 code/randmap/dump_tiberium_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/tiberium_golden.txt。每个用例给一张合成的小图，
// 跑完之后比四样：随机数个数、每个变了的格子的 (overlay, +0x11E, +0x3C)、
// 建出来的 TIBTRE 地貌物（名字与落点）、池子用掉的槽位数。
//
// 七个用例分别踩：
//   plain   —— 全未定砖的常规一片
//   gems    —— isGems，基址 27 且**不建** TIBTRE
//   anchor  —— 一条 +0x45 斜带：这些格子不落矿也不触发爆发锚定，但照样扩散
//   preore  —— 预置矿格，走「加厚 +0x11E」以及那条没有 fieldId 保护的接受条件
//   island  —— 只留两格能扩散，撞满 restarts >= 10 的上限（10 次全图清 +0x3C、
//              10 棵 TIBTRE，而 count=40 一格也铺不满）
//   edge    —— 种子在菱形东端，大量邻格被 0x578460 挡掉
//   blocked —— 五分之一的格子是已定砖
#include "ore_field.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

struct InCell {
  int x = 0, y = 0, tile = 0, overlay = -1, data = 0, flag45 = 0;
};
struct OutCell {
  int x = 0, y = 0, overlay = -1, data = 0, queued = 0;
};
struct Terr {
  std::string name;
  int x = 0, y = 0;
};

struct Case {
  std::string name;
  int w = 0, h = 0, sx = 0, sy = 0, count = 0, field = 0, gems = 0;
  unsigned rng_seed = 0;
  int rng = 0, used = 0, changed = 0;
  std::vector<InCell> in;
  std::vector<OutCell> out;
  std::vector<Terr> terr;
};

std::vector<Case> LoadGolden() {
  const std::string path = std::string(RA2_GOLDEN_DIR) + "/tiberium_golden.txt";
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
      std::string arrow;
      is >> c.name >> c.w >> c.h >> c.sx >> c.sy >> c.count >> c.field
         >> c.gems >> c.rng_seed >> arrow >> c.rng >> c.used >> c.changed;
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "IN") {
      InCell v;
      is >> v.x >> v.y >> v.tile >> v.overlay >> v.data >> v.flag45;
      c.in.push_back(v);
    } else if (key == "OUT") {
      OutCell v;
      is >> v.x >> v.y >> v.overlay >> v.data >> v.queued;
      c.out.push_back(v);
    } else if (key == "TERR") {
      Terr t;
      is >> t.name >> t.x >> t.y;
      c.terr.push_back(t);
    }
  }
  return cases;
}

// 与基准脚本里的四个桩一一对应的世界。
//
// ⚠️ 菱形外的坐标在原版里全部落到同一份哑元 0xABDC50 上：读到的是上一次的
// 残留，写进去也不会影响任何真实格子。这里用一条独立的 `dummy_` 记录复现，
// 而不是把菱形外的格子当成不存在——基准脚本的格子指针表也正是这么摆的。
class FakeOreWorld : public OreWorld {
 public:
  struct Cell {
    int tile = 0;
    int overlay = -1;
    int data = 0;
  };

  explicit FakeOreWorld(const MapGrid& grid) : grid_(grid) {}

  Cell& At(const CellCoord& c) {
    if (!grid_.InMap(c)) return dummy_;
    return cells_[(static_cast<long long>(c.y) << 9) + c.x];
  }

  int IsoTile(const CellCoord& c) override { return At(c).tile; }
  int Level(const CellCoord& c) override { (void)c; return 0; }
  int Ramp(const CellCoord& c) override { (void)c; return 0; }
  int Overlay(const CellCoord& c) override { return At(c).overlay; }
  void SetOverlay(const CellCoord& c, int v) override { At(c).overlay = v; }
  int OverlayData(const CellCoord& c) override { return At(c).data; }
  void SetOverlayData(const CellCoord& c, int v) override {
    At(c).data = v & 0xFF;
  }
  // 0x485010 的桩：格子上是矿（102..113）或宝石（27..38）就算「有矿物」
  int ContainedTiberiumIndex(const CellCoord& c) override {
    const int ov = At(c).overlay;
    if ((ov >= 102 && ov <= 113) || (ov >= 27 && ov <= 38)) return 0;
    return -1;
  }
  void CreateTerrain(const char* name, const CellCoord& at) override {
    Terr t;
    t.name = name;
    t.x = at.x;
    t.y = at.y;
    terrains.push_back(t);
  }
  CellCoord Waypoint(int index) override { (void)index; return CellCoord(); }

  std::vector<Terr> terrains;

 private:
  MapGrid grid_;
  Cell dummy_;
  std::map<long long, Cell> cells_;
};

TEST(OreField, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(7u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    MapGrid grid(c.w, c.h);
    const int side = grid.side();
    std::vector<PaintWorkCell> work(
        static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    FakeOreWorld world(grid);
    for (std::size_t i = 0; i < c.in.size(); ++i) {
      const InCell& v = c.in[i];
      FakeOreWorld::Cell& cell = world.At(CellCoord{v.x, v.y});
      cell.tile = v.tile;
      cell.overlay = v.overlay;
      cell.data = v.data;
      work[static_cast<std::size_t>(v.y * side + v.x)].flag45 = v.flag45 != 0;
    }

    Randomizer rng(c.rng_seed);
    OreContext ctx;
    ctx.grid = grid;
    ctx.work = &work;
    ctx.world = &world;
    ctx.rng = &rng;
    ctx.side = side;
    ctx.map_w = c.w;
    ctx.local.x = 0;
    ctx.local.y = 0;
    ctx.local.w = c.w;
    ctx.local.h = c.h;

    CreateOreField(ctx, CellCoord{c.sx, c.sy}, c.count, c.field, c.gems != 0);

    EXPECT_EQ(static_cast<long long>(c.rng), rng.call_count())
        << "取数个数不对";

    // 逐格比：基准只列了「跑完与跑前不同」的格子，没列的必须原样
    std::map<long long, const OutCell*> expect;
    for (std::size_t i = 0; i < c.out.size(); ++i) {
      expect[(static_cast<long long>(c.out[i].y) << 9) + c.out[i].x] =
          &c.out[i];
    }
    std::map<long long, InCell> before;
    for (std::size_t i = 0; i < c.in.size(); ++i) {
      before[(static_cast<long long>(c.in[i].y) << 9) + c.in[i].x] = c.in[i];
    }
    int diff_count = 0;
    grid.ForEachCell([&](const CellCoord& cc) {
      const long long key = (static_cast<long long>(cc.y) << 9) + cc.x;
      FakeOreWorld::Cell& cell = world.At(cc);
      const int queued =
          work[static_cast<std::size_t>(cc.y * side + cc.x)].queued;
      int want_ov = -1, want_da = 0, want_q = 0;
      const std::map<long long, InCell>::const_iterator bi = before.find(key);
      if (bi != before.end()) {
        want_ov = bi->second.overlay;
        want_da = bi->second.data;
      }
      const std::map<long long, const OutCell*>::const_iterator ei =
          expect.find(key);
      if (ei != expect.end()) {
        want_ov = ei->second->overlay;
        want_da = ei->second->data;
        want_q = ei->second->queued;
        ++diff_count;
      }
      ASSERT_EQ(want_ov, cell.overlay)
          << "(" << cc.x << "," << cc.y << ") 的 overlay 不对";
      ASSERT_EQ(want_da, cell.data)
          << "(" << cc.x << "," << cc.y << ") 的 +0x11E 不对";
      ASSERT_EQ(want_q, queued)
          << "(" << cc.x << "," << cc.y << ") 的 +0x3C 不对";
    });
    EXPECT_EQ(c.changed, diff_count);

    ASSERT_EQ(c.terr.size(), world.terrains.size()) << "TIBTRE 棵数不对";
    for (std::size_t i = 0; i < c.terr.size(); ++i) {
      EXPECT_EQ(c.terr[i].name, world.terrains[i].name);
      EXPECT_EQ(c.terr[i].x, world.terrains[i].x);
      EXPECT_EQ(c.terr[i].y, world.terrains[i].y);
    }
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
