// 死代码复现 —— 0x5A82E0 随机布点放置器黄金对拍
//
// 基准由 .qwen/tmp/_golden_5A82E0.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_placer_golden.txt。格子侧（GetCellAt、
// IsPlain、IsWithinUsableArea、cell+0xE4/+0x11B/+0xEC）与 0x45E7B0 名字表
// 查找在基准侧全是真身；只有 BuildingClass 那一侧打了桩，下面的
// FakePlacerWorld 用**同一套桩语义**：
//   · operator new 恒成功，0x43B740 只记下名字表解析出的类型下标
//   · 占地表由用例给定，(0x7FFF, 0x7FFF) 收尾
//   · Unlimbo 记下落点；成功时把占地格的 +0xE4 写成非 0（只写菱形内）
//   · Destroy 只计数
//   · FindNeutralOwner 返回 0（基准侧 0x5117D0→0、0x502D30→0x11110000）
//   · LookupTypeIndex 只在前 name_count 个类型名里找（0x45E7B0 的
//     [0xA83C6C] 表只灌了 name_count 项）——查不到返回 -1
// ⚠️ -1 时原版跳过 attempt++ 会死循环（零引用死代码的根因，黄金不覆盖）；
//    C++ 模型照抄（continue 语义 = 原版 0x5A8489 je 0x5A86E1）。
//
// 八个用例分别踩：
//   simple    1x1 占地、this38=0（下界 0）、全 36 名
//   all38     this38=1（下界 15）、2x2 占地
//   blocked   已定砖 + 有占用者的格子交错
//   levels    层高 (x+y)%3：占地永远跨层 -> 60 次尝试全废、对象被销毁
//   veto      +0xEC == 3 与工作记录 +0x45 两条否决
//   unplace   Unlimbo 恒失败 -> 每栋耗满 60 次尝试
//   halfname  名字表只 20 项（后半段名字查不到）
//   no40      全图超过 X+Y>40 的格子已定砖
#include "dead_code_placer.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "map_grid.h"
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
  int w = 0, h = 0, unlimbo = 0, this38 = 0, name_count = 0;
  unsigned rng_seed = 0;
  int rng = 0, placed = 0, destroyed = 0;
  std::vector<PlacerFoundationOffset> foundation;
  std::vector<InCell> in;
  std::vector<int> types;
  std::vector<std::pair<int, int> > places, occ;
};

std::vector<Case> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/deadcode_placer_golden.txt";
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
      is >> c.name >> c.w >> c.h >> c.rng_seed >> c.unlimbo >> fnd
         >> c.this38 >> c.name_count >> arrow >> c.rng >> c.placed
         >> c.destroyed;
      for (std::size_t i = 0; i < fnd.size();) {
        const std::size_t colon = fnd.find(':', i);
        std::size_t comma = fnd.find(',', colon);
        if (comma == std::string::npos) comma = fnd.size();
        PlacerFoundationOffset o;
        o.dx = static_cast<std::int16_t>(
            std::atoi(fnd.substr(i, colon - i).c_str()));
        o.dy = static_cast<std::int16_t>(
            std::atoi(fnd.substr(colon + 1, comma - colon - 1).c_str()));
        c.foundation.push_back(o);
        i = comma + 1;
      }
      PlacerFoundationOffset end;
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

class FakePlacerWorld : public DeadPlacerWorld {
 public:
  struct Cell {
    int tile = 0;
    int level = 0;
    int ec = 0;
    int occupier = 0;
  };

  FakePlacerWorld(const MapGrid& grid, const Case& c)
      : grid_(grid), case_(&c) {
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
    // 0x578540(cell, 1)
    return IsWithinUsableArea(grid_.width(), local_, c.x, c.y, At(c).level,
                              0, true);
  }

  // 0x45E7B0：只在前 name_count 个类型名里找（基准侧 [0xA83C6C] 表灌了
  // name_count 项）。strcmpi 比较，命中返索引、未命中 -1。
  int LookupTypeIndex(const std::string& name) override {
    const int count = case_->name_count;
    for (int i = 0; i < count; ++i) {
      if (name == kPlacerTypeNames[i]) return i;
    }
    return -1;
  }

  bool AllocBuildingMemory() override { return true; }
  int CreateBuilding(int type_index, int owner) override {
    (void)owner;
    types.push_back(type_index);
    return static_cast<int>(types.size()) - 1;
  }
  const std::vector<PlacerFoundationOffset>& Foundation(int handle) override {
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
  int FindNeutralOwner() override { return 0; }

  std::vector<int> types;
  std::vector<std::pair<int, int> > places;
  int destroyed = 0;

 private:
  MapGrid grid_;
  const Case* case_;
  MapRect local_;
  Cell dummy_;
  std::map<long long, Cell> cells_;
};

TEST(DeadCodePlacer, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(8u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    MapGrid grid(c.w, c.h);
    FakePlacerWorld world(grid, c);
    for (std::size_t i = 0; i < c.in.size(); ++i) {
      const InCell& v = c.in[i];
      FakePlacerWorld::Cell& cell = world.At(CellCoord{v.x, v.y});
      cell.tile = v.tile;
      cell.level = v.level;
      cell.ec = v.ec;
      cell.occupier = v.e4;
    }

    Randomizer rng(c.rng_seed);
    DeadPlacerContext ctx;
    ctx.rng = &rng;
    ctx.world = &world;
    ctx.anchor = CellCoord{10, 20};
    ctx.this38 = c.this38;
    ctx.type_names = nullptr;       // 用头文件内建的 kPlacerTypeNames

    DeadCodeRandomPlacer(ctx);

    EXPECT_EQ(static_cast<long long>(c.rng), rng.call_count())
        << "取数个数不对";
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
      if (world.At(cc).occupier != 0)
        occ.push_back(std::make_pair(cc.x, cc.y));
    });
    // 基准按 _diamond() 的 (y, x) 字典序输出，这里按同一顺序排一下
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