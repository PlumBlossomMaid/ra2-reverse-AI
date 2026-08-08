// 阶段 8 外层 0x5A23A0 PlaceTiberium 的黄金对拍
//
// 基准由 code/randmap/dump_tiberium2_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/tiberium2_golden.txt。内层 0x5A28C0 已经由
// test_ore_field 单独对过，这一份对的是**外壳**：真值表、参考点、最近候选点、
// 矿量公式、正态抖动窗口、出生点补偿，以及 wpBase 的推进规则。
//
// 十六个用例分别踩：
//   mt1-res1 / mt0-res1 / mt0-res3 / mt2-res0 / mt3-res3 / mt1-res3 / mt9-res1
//            —— 0x5A28A0 那张五路跳转表的每一条出口，外加宝石开关的组合
//   nullcand / nullcand0  —— RegionClass +0x00 是空指针：整段跳过，wpBase 不推进
//   emptycand / emptycand0—— 指针非空但零元素：先跑参考点再跳。⚠️ 与上面一对
//            在 sp == 0 时随机数个数不同（1625 vs 1435），这正是
//            「0x5A23F1 判指针、0x5A25A7 判个数」的实证
//   sp0      —— 0 个出生点的区域：参考点走 PickBelowMul，矿量乘子按 0.5 算
//   three    —— 三个区域、中间那个空指针：检验 wpBase 的累加顺序
//   zerotib  —— total == 0：连出生点门口那几片都不布
//   tinytib  —— avg == 0：正态抖动一半是负数，size < 0 的候选点被跳过（随机数照花）
//   bigtib   —— 矿量拉满
#include "ore_field.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "scaled_random.h"   // FpuChopScope
#include "gtest/gtest.h"
#include "regions.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

struct OutCell {
  int x = 0, y = 0, overlay = -1, data = 0, queued = 0;
};
struct Terr {
  std::string name;
  int x = 0, y = 0;
};
struct RegSpec {
  int start_points = 0;
  bool created = false;                 // +0x00 是不是非空指针
  std::vector<CellCoord> cand;
  std::vector<CellCoord> cells;
};

struct Case {
  std::string name;
  int w = 0, h = 0, mt = 0, res = 0, tib = 0, mn = 0, mx = 0;
  unsigned rng_seed = 0;
  int rng = 0, n_terr = 0, changed = 0;
  std::vector<CellCoord> wp;
  std::vector<RegSpec> regs;
  std::vector<Terr> terr;
  std::vector<OutCell> out;
};

std::vector<CellCoord> ParsePoints(const std::string& s) {
  std::vector<CellCoord> v;
  if (s == "-" || s == ".") return v;
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t colon = s.find(':', i);
    std::size_t comma = s.find(',', i);
    if (comma == std::string::npos) comma = s.size();
    CellCoord c;
    c.x = static_cast<std::int16_t>(std::atoi(s.substr(i, colon - i).c_str()));
    c.y = static_cast<std::int16_t>(
        std::atoi(s.substr(colon + 1, comma - colon - 1).c_str()));
    v.push_back(c);
    i = comma + 1;
  }
  return v;
}

std::vector<Case> LoadGolden() {
  const std::string path = std::string(RA2_GOLDEN_DIR) + "/tiberium2_golden.txt";
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
      is >> c.name >> c.w >> c.h >> c.mt >> c.res >> c.tib >> c.mn >> c.mx
         >> c.rng_seed >> arrow >> c.rng >> c.n_terr >> c.changed;
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "WP") {
      int i = 0, x = 0, y = 0;
      is >> i >> x >> y;
      c.wp.push_back(CellCoord{static_cast<std::int16_t>(x),
                               static_cast<std::int16_t>(y)});
    } else if (key == "REG") {
      RegSpec r;
      int i = 0;
      is >> i >> r.start_points;
      c.regs.push_back(r);
    } else if (key == "CAND") {
      std::string body;
      is >> body;
      c.regs.back().created = (body != "-");
      c.regs.back().cand = ParsePoints(body);
    } else if (key == "CELLS") {
      std::string body;
      is >> body;
      c.regs.back().cells = ParsePoints(body);
    } else if (key == "TERR") {
      Terr t;
      is >> t.name >> t.x >> t.y;
      c.terr.push_back(t);
    } else if (key == "OUT") {
      OutCell v;
      is >> v.x >> v.y >> v.overlay >> v.data >> v.queued;
      c.out.push_back(v);
    }
  }
  return cases;
}

// 与基准脚本的四个桩一一对应。菱形外的坐标全部落到同一份哑元 0xABDC50 上。
class FakeOreWorld : public OreWorld {
 public:
  struct Cell {
    int tile = 0;
    int overlay = -1;
    int data = 0;
  };

  FakeOreWorld(const MapGrid& grid, const std::vector<CellCoord>* wp)
      : grid_(grid), wp_(wp) {}

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
  // 0x68BCC0 是**裸下标**，越界就读到表外；基准里预置了 32 条，够用。
  CellCoord Waypoint(int index) override {
    EXPECT_GE(index, 0);
    EXPECT_LT(index, static_cast<int>(wp_->size())) << "waypoint 下标越界";
    return (*wp_)[static_cast<std::size_t>(index)];
  }

  std::vector<Terr> terrains;

 private:
  MapGrid grid_;
  const std::vector<CellCoord>* wp_;
  Cell dummy_;
  std::map<long long, Cell> cells_;
};

TEST(PlaceTiberium, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(16u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    MapGrid grid(c.w, c.h);
    const int side = grid.side();
    std::vector<PaintWorkCell> work(
        static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    FakeOreWorld world(grid, &c.wp);

    RegionSet regions;
    for (std::size_t i = 0; i < c.regs.size(); ++i) {
      RegionClass* r = regions.Create(CellCoord{0, 0}, 0, false);
      r->start_points = c.regs[i].start_points;
      r->candidates_created = c.regs[i].created;
      r->candidates = c.regs[i].cand;
      r->cells = c.regs[i].cells;
    }

    Randomizer rng(c.rng_seed);
    NormalDeviate normal(rng);
    OreContext ctx;
    ctx.grid = grid;
    ctx.work = &work;
    ctx.world = &world;
    ctx.rng = &rng;
    ctx.normal = &normal;
    ctx.side = side;
    ctx.map_w = c.w;
    ctx.local.x = 0;
    ctx.local.y = 0;
    ctx.local.w = c.w;
    ctx.local.h = c.h;

    TiberiumParams p;
    p.map_type = c.mt;
    p.resources = c.res;
    p.tiberium = c.tib;
    p.min_tiberium = c.mn;
    p.max_tiberium = c.mx;

    {
      FpuChopScope chop;                     // 模拟器的 FPCW 是 0x0E7F
      PlaceTiberium(ctx, regions, p);
    }

    EXPECT_EQ(static_cast<long long>(c.rng), rng.call_count()) << "取数个数不对";

    // 逐格比：基准只列了「跑完与跑前不同」的格子，没列的必须原样（-1 / 0 / 0）
    std::map<long long, const OutCell*> expect;
    for (std::size_t i = 0; i < c.out.size(); ++i) {
      expect[(static_cast<long long>(c.out[i].y) << 9) + c.out[i].x] = &c.out[i];
    }
    int diff_count = 0;
    grid.ForEachCell([&](const CellCoord& cc) {
      const long long key = (static_cast<long long>(cc.y) << 9) + cc.x;
      FakeOreWorld::Cell& cell = world.At(cc);
      const int queued =
          work[static_cast<std::size_t>(cc.y * side + cc.x)].queued;
      int want_ov = -1, want_da = 0, want_q = 0;
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
    ASSERT_EQ(static_cast<std::size_t>(c.n_terr), world.terrains.size());
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
