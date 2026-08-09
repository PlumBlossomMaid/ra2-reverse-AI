// 阶段 7「Adding tech buildings」分支 B（0x5A95B0 的 MapType != 2 那半边）的黄金对拍
//
// 基准由 code/randmap/dump_tech_buildings_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/tech_buildings_golden.txt。摸格子的 PickRange、
// CellStruct 的相等判断、GetCellAt、IsPlain、IsWithinUsableArea 在基准侧全是真身；
// 只有 BuildingClass 那一侧打了桩，下面的 FakeTechWorld 用**同一套桩语义**：
//   · operator new 恒成功，BuildingClass 构造只记下抽到的种类下标
//   · 占地表由用例给定，(0x7FFF, 0x7FFF) 收尾
//   · Unlimbo 记下落点；成功时把占地格的 +0xE4 写成非 0
//     ⚠️ 只写**菱形内**的格子——菱形外落到共享哑元，基准侧的桩也没写它
//   · Destroy 只计数
//
// 七个用例分别踩：
//   simple      1x1 占地，一次就落地
//   bigfoot     2x2 占地（其中一格落在菱形外，读的是哑元 —— 照样通过）
//   blocked     已定砖 + 有占用者的格子交错
//   levels      层高 (x+y)%3：2x2 占地永远跨层 -> 100 次尝试全废、对象被销毁
//   veto        +0xEC == 3 与工作记录 +0x45 两条否决
//   unplaceable Unlimbo 恒失败 -> 每栋耗满 100 次尝试
//   nowhere     全图已定砖 -> 每次尝试摸满 200 次，然后强行用格 (0,0)
#include "tech_buildings.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "zones.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

struct InCell {
  int x = 0, y = 0, tile = 0, level = 0, ec = 0, e4 = 0, flag45 = 0;
};

struct Case {
  std::string name;
  int w = 0, h = 0, unlimbo = 0, type_count = 0;
  unsigned rng_seed = 0;
  int rng = 0, placed = 0, destroyed = 0;
  std::vector<FoundationOffset> foundation;
  std::vector<InCell> in;
  std::vector<int> types;
  std::vector<std::pair<int, int> > places, occ;
};

std::vector<Case> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/tech_buildings_golden.txt";
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
      std::string fnd, arrow;
      is >> c.name >> c.w >> c.h >> c.rng_seed >> c.unlimbo >> c.type_count
         >> fnd >> arrow >> c.rng >> c.placed >> c.destroyed;
      for (std::size_t i = 0; i < fnd.size();) {
        const std::size_t colon = fnd.find(':', i);
        std::size_t comma = fnd.find(',', colon);
        if (comma == std::string::npos) comma = fnd.size();
        FoundationOffset o;
        o.dx = std::atoi(fnd.substr(i, colon - i).c_str());
        o.dy = std::atoi(fnd.substr(colon + 1, comma - colon - 1).c_str());
        c.foundation.push_back(o);
        i = comma + 1;
      }
      FoundationOffset end;
      end.dx = 0x7FFF;
      end.dy = 0x7FFF;
      c.foundation.push_back(end);
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "IN") {
      InCell v;
      is >> v.x >> v.y >> v.tile >> v.level >> v.ec >> v.e4 >> v.flag45;
      c.in.push_back(v);
    } else if (key == "TYPE") {
      int k = 0;
      is >> k;
      c.types.push_back(k);
    } else if (key == "PLACE" || key == "OCC") {
      int x = 0, y = 0;
      is >> x >> y;
      (key == "PLACE" ? c.places : c.occ).push_back(std::make_pair(x, y));
    }
  }
  return cases;
}

class FakeTechWorld : public TechWorld {
 public:
  struct Cell {
    int tile = 0;
    int level = 0;
    int ec = 0;
    int occupier = 0;
  };

  FakeTechWorld(const MapGrid& grid, const Case& c) : grid_(grid), case_(&c) {
    local_.x = 0;
    local_.y = 0;
    local_.w = grid.width();
    local_.h = grid.height();
  }

  Cell& At(const CellCoord& c) {
    if (!grid_.InMap(c)) return dummy_;    // 共享哑元 0xABDC50，全零
    return cells_[(static_cast<long long>(c.y) << 9) + c.x];
  }

  int Level(const CellCoord& c) override { return At(c).level; }
  bool IsPlain(const CellCoord& c) override {                 // 0x486380
    const int t = At(c).tile;
    return t == 0xFFFF || t == 0;
  }
  bool HasOccupier(const CellCoord& c) override {
    return At(c).occupier != 0;
  }
  int Field0xEC(const CellCoord& c) override { return At(c).ec; }
  bool WithinUsableArea(const CellCoord& c) override {
    // 0x578540(cell, 1)：与 0x578460 同一套判据，层高/坡面取的是该格自己的
    // +0x11B / +0x11C（出图就是哑元里的残留）
    return IsWithinUsableArea(grid_.width(), local_, c.x, c.y, At(c).level,
                              0, true);
  }

  int NeutralTechBuildingCount() override { return case_->type_count; }
  bool AllocBuildingMemory() override { return true; }
  int CreateBuilding(int type_index) override {
    types.push_back(type_index);
    return static_cast<int>(types.size()) - 1;
  }
  const std::vector<FoundationOffset>& Foundation(int handle) override {
    (void)handle;
    return case_->foundation;
  }
  bool Unlimbo(int handle, const CellCoord& at) override {
    (void)handle;
    if (!case_->unlimbo) return false;
    places.push_back(std::make_pair(at.x, at.y));
    for (std::size_t i = 0; i + 1 < case_->foundation.size(); ++i) {
      const CellCoord n{at.x + case_->foundation[i].dx,
                        at.y + case_->foundation[i].dy};
      if (grid_.InMap(n)) At(n).occupier = 1;
    }
    return true;
  }
  void Destroy(int handle) override {
    (void)handle;
    ++destroyed;
  }
  void PlaceRandomBuilding(RegionClass* r) override { placed_regions.push_back(r); }

  std::vector<int> types;
  std::vector<std::pair<int, int> > places;
  std::vector<RegionClass*> placed_regions;   // 分支 A：0x595400 的调用序列
  int destroyed = 0;

 private:
  MapGrid grid_;
  const Case* case_;
  MapRect local_;
  Cell dummy_;
  std::map<long long, Cell> cells_;
};

// 分支 A（MapType == 2）的一条基准：区域表里每个区的 +0x20，以及原版实际
// 调用 0x595400 时传进去的区域下标序列。
struct ACase {
  std::string name;
  int w = 0, h = 0;
  unsigned rng_seed = 0;
  int rng = 0;
  std::vector<int> starts;   // 每个区域的 +0x20
  std::vector<int> seq;      // 0x595400 收到的区域下标，按调用顺序
};

std::vector<int> ParseCsv(const std::string& s) {
  std::vector<int> v;
  if (s == "-") return v;
  for (std::size_t i = 0; i < s.size();) {
    std::size_t comma = s.find(',', i);
    if (comma == std::string::npos) comma = s.size();
    v.push_back(std::atoi(s.substr(i, comma - i).c_str()));
    i = comma + 1;
  }
  return v;
}

std::vector<ACase> LoadAGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/tech_buildings_golden.txt";
  std::ifstream f(path.c_str());
  EXPECT_TRUE(f.good()) << "打不开基准档 " << path;
  std::vector<ACase> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream is(line);
    std::string key;
    is >> key;
    if (key != "ACASE") continue;
    ACase c;
    std::string starts, arrow, seq;
    is >> c.name >> c.w >> c.h >> c.rng_seed >> starts >> arrow >> c.rng >> seq;
    c.starts = ParseCsv(starts);
    c.seq = ParseCsv(seq);
    cases.push_back(c);
  }
  return cases;
}

TEST(TechBuildings, BranchAMatchesOriginal) {
  const std::vector<ACase> cases = LoadAGolden();
  ASSERT_EQ(5u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const ACase& c = cases[ci];
    SCOPED_TRACE(c.name);

    MapGrid grid(c.w, c.h);
    const int side = grid.side();
    std::vector<PaintWorkCell> work(
        static_cast<std::size_t>(side) * static_cast<std::size_t>(side));

    Case unused;                       // 分支 A 一个字段都不碰
    FakeTechWorld world(grid, unused);

    RegionSet regions;
    std::vector<RegionClass*> order;
    for (std::size_t i = 0; i < c.starts.size(); ++i) {
      RegionClass* r = regions.Create(CellCoord{0, 0}, 0, false);
      r->start_points = c.starts[i];
      order.push_back(r);
    }

    Randomizer rng(c.rng_seed);
    TechContext ctx;
    ctx.side = side;
    ctx.work = &work;
    ctx.regions = &regions;
    ctx.rng = &rng;
    ctx.world = &world;
    ctx.map_type = 2;

    AddTechBuildings(ctx);

    EXPECT_EQ(static_cast<long long>(c.rng), rng.call_count()) << "取数个数不对";
    EXPECT_EQ(0, world.destroyed);
    EXPECT_TRUE(world.types.empty()) << "分支 A 不该造建筑对象";
    ASSERT_EQ(c.seq.size(), world.placed_regions.size()) << "0x595400 调用次数不对";
    for (std::size_t i = 0; i < c.seq.size(); ++i) {
      const std::size_t want = static_cast<std::size_t>(c.seq[i]);
      ASSERT_LT(want, order.size());
      EXPECT_EQ(order[want], world.placed_regions[i]) << "第 " << i << " 次调用的区域不对";
    }
  }
}

TEST(TechBuildings, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(7u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    MapGrid grid(c.w, c.h);
    const int side = grid.side();
    std::vector<PaintWorkCell> work(
        static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    // 阶段 1 只给菱形内的格子填打包坐标；其余槽位恒为 (0,0)，正是被拒绝的那批
    grid.ForEachCell([&](const CellCoord& cc) {
      work[static_cast<std::size_t>(cc.y * side + cc.x)].coord = cc;
    });

    FakeTechWorld world(grid, c);
    for (std::size_t i = 0; i < c.in.size(); ++i) {
      const InCell& v = c.in[i];
      FakeTechWorld::Cell& cell = world.At(CellCoord{v.x, v.y});
      cell.tile = v.tile;
      cell.level = v.level;
      cell.ec = v.ec;
      cell.occupier = v.e4;
      work[static_cast<std::size_t>(v.y * side + v.x)].flag45 = v.flag45 != 0;
    }

    Randomizer rng(c.rng_seed);
    RegionSet regions;
    TechContext ctx;
    ctx.side = side;
    ctx.work = &work;
    ctx.regions = &regions;
    ctx.rng = &rng;
    ctx.world = &world;
    ctx.map_type = 1;

    AddTechBuildings(ctx);

    EXPECT_EQ(static_cast<long long>(c.rng), rng.call_count()) << "取数个数不对";
    EXPECT_EQ(c.destroyed, world.destroyed);
    ASSERT_EQ(c.types.size(), world.types.size()) << "建了几栋不对";
    for (std::size_t i = 0; i < c.types.size(); ++i) {
      EXPECT_EQ(c.types[i], world.types[i]) << "第 " << i << " 栋的种类不对";
    }
    ASSERT_EQ(c.places.size(), world.places.size()) << "落点个数不对";
    for (std::size_t i = 0; i < c.places.size(); ++i) {
      EXPECT_EQ(c.places[i].first, world.places[i].first);
      EXPECT_EQ(c.places[i].second, world.places[i].second);
    }

    std::vector<std::pair<int, int> > occ;
    grid.ForEachCell([&](const CellCoord& cc) {
      if (world.At(cc).occupier != 0) occ.push_back(std::make_pair(cc.x, cc.y));
    });
    // 基准是按 _diamond() 的 (y, x) 字典序输出的，这里按同一顺序排一下
    std::sort(occ.begin(), occ.end(),
              [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                if (a.second != b.second) return a.second < b.second;
                return a.first < b.first;
              });
    ASSERT_EQ(c.occ.size(), occ.size()) << "被占住的格数不对";
    for (std::size_t i = 0; i < occ.size(); ++i) {
      EXPECT_EQ(c.occ[i].first, occ[i].first);
      EXPECT_EQ(c.occ[i].second, occ[i].second);
    }
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
