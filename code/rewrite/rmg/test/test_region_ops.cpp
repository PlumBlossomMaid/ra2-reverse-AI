// 两个通用区域算子 0x5A0410 Erode / 0x5A0160 Dilate 的黄金对拍
//
// 基准由 code/randmap/dump_region_ops_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/region_ops_golden.txt。每个用例给一张输入
// （IN 行：tile / subtile / level / 工作记录 +0x38 归属 / +0x3C 戳），
// 调一次算子之后比三样：
//   1) 返回值 —— Erode 恒 1；Dilate 的 0 就是"和别人贴上了"的隔离测试结果
//   2) 每格终态 (tile, subtile, level, +0x38, +0x3C)
//   3) 共享哑元 0xABDC50 的 +0x24
//
// ⚠️ 两个函数**都不取随机数**，所以这里没有取数个数这一项；
// 生成脚本那边用 assert 守住了这一点。
//
// 用例里几条是专门盯着容易被"简化掉"的行为造的：
//   e-dirty-r1 / e-dirty-r3  —— rings <= 1 时原版**不清** +0x3C，脏戳原样留着
//   e-selfnew                —— newOwner == owner，下一圈全靠戳挡住重复收集
//   d-rect-empty             —— 矩形宽高为 0：被夹掉的邻格是 continue，不是失败
//   d-solid-false / d-wall-false —— 邻格无主但砖不是未定砖，照样判失败
//   d-flag-lvl               —— flag 那条：owner-1 的水/岸也收，未定砖再压层高
#include "regions.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
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

TheaterData Temperate() {
  TheaterData t;
  t.water_set = 314;
  t.shore_pieces = 89;
  t.green_tile = 493;
  t.clear_to_green_lat = 494;
  t.waterfall_east = 539;
  t.waterfall_west = 547;
  t.waterfall_north = 543;
  t.waterfall_south = 414;
  return t;
}

constexpr int kCellSlots = 0x40000;   // 引擎侧格子数组是 512×512 定长

int SlotOf(const CellCoord& c) {
  const int idx = (c.y << 9) + c.x;
  return (idx < 0 || idx >= kCellSlots) ? -1 : idx;
}

struct Cell {
  int tile = 0;
  int subtile = 0;
  std::int8_t level = 0;
};

// 与 test_regions.cpp 同一份最小格子世界：格子不存在时读写都落到同一份哑元。
class RegionStub : public RegionWorld {
 public:
  explicit RegionStub(const MapGrid& g)
      : cells_(kCellSlots), exists_(kCellSlots, 0) {
    g.ForEachCell([&](const CellCoord& c) { exists_[SlotOf(c)] = 1; });
  }

  bool CellExists(const CellCoord& c) const override {
    const int s = SlotOf(c);
    return s >= 0 && exists_[s] != 0;
  }
  void SetDummyCoord(const CellCoord& c) override { dummy_coord_ = c; }

  int IsoTile(const CellCoord& c) const override { return At(c).tile; }
  void SetIsoTile(const CellCoord& c, int v) override { Mut(c).tile = v; }
  void SetSubTile(const CellCoord& c, int v) override { Mut(c).subtile = v; }
  int Level(const CellCoord& c) const override { return At(c).level; }
  void SetLevel(const CellCoord& c, int v) override {
    Mut(c).level = static_cast<std::int8_t>(v);
  }

  Cell Get(const CellCoord& c) const { return At(c); }
  void Seed(const CellCoord& c, int tile, int subtile, int level) {
    Cell& x = Mut(c);
    x.tile = tile;
    x.subtile = subtile;
    x.level = static_cast<std::int8_t>(level);
  }
  CellCoord dummy_coord() const { return dummy_coord_; }

 private:
  const Cell& At(const CellCoord& c) const {
    const int s = SlotOf(c);
    return (s < 0 || !exists_[s]) ? dummy_ : cells_[s];
  }
  Cell& Mut(const CellCoord& c) {
    const int s = SlotOf(c);
    return (s < 0 || !exists_[s]) ? dummy_ : cells_[s];
  }

  std::vector<Cell> cells_;
  std::vector<char> exists_;
  Cell dummy_;
  CellCoord dummy_coord_;
};

struct GoldenCase {
  std::string op;      // ERODE / DILATE
  std::string name;
  int w = 0, h = 0;
  int level30c = 0, owner = 0, rings = 0, new_owner = 0;
  int rect_x = 0, rect_y = 0, rect_w = 0, rect_h = 0;
  int flag = 0, cell_val = 0;
  int ret = 0;
  CellCoord dummy;
  std::vector<std::array<int, 7>> in;    // x y tile subtile level owner queued
  std::vector<std::array<int, 7>> out;
};

// "key=123" -> 123。key 对不上直接把用例判失败，不静默吞。
int NamedInt(const std::string& tok, const char* key) {
  const std::size_t n = std::strlen(key);
  EXPECT_EQ(tok.compare(0, n, key), 0) << "expected " << key << " got " << tok;
  return std::atoi(tok.c_str() + n);
}

bool Has(const std::string& tok, const char* key) {
  return tok.compare(0, std::strlen(key), key) == 0;
}

std::vector<GoldenCase> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/region_ops_golden.txt";
  std::ifstream in(path);
  EXPECT_TRUE(in.good()) << "cannot open " << path;
  std::vector<GoldenCase> out;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "CASE") {
      GoldenCase c;
      ss >> c.op >> c.name >> c.w >> c.h;
      std::string tok;
      while (ss >> tok) {
        if (Has(tok, "level30c=")) {
          c.level30c = NamedInt(tok, "level30c=");
        } else if (Has(tok, "owners=") || Has(tok, "terrain=") ||
                   Has(tok, "stamp=")) {
          continue;                       // 只是记录输入怎么造的，不参与断言
        } else if (Has(tok, "owner=")) {
          c.owner = NamedInt(tok, "owner=");
        } else if (Has(tok, "rings=")) {
          c.rings = NamedInt(tok, "rings=");
        } else if (Has(tok, "new=")) {
          c.new_owner = NamedInt(tok, "new=");
        } else if (Has(tok, "rect=")) {
          std::sscanf(tok.c_str(), "rect=%d,%d,%d,%d", &c.rect_x, &c.rect_y,
                      &c.rect_w, &c.rect_h);
        } else if (Has(tok, "flag=")) {
          c.flag = NamedInt(tok, "flag=");
        } else if (Has(tok, "cellval=")) {
          c.cell_val = NamedInt(tok, "cellval=");
        } else if (Has(tok, "ret=")) {
          c.ret = NamedInt(tok, "ret=");
        } else if (Has(tok, "dummy=")) {
          std::sscanf(tok.c_str(), "dummy=%d,%d", &c.dummy.x, &c.dummy.y);
        }
      }
      out.push_back(c);
    } else if (tag == "IN" || tag == "OUT") {
      std::array<int, 7> v{};
      for (int& f : v) ss >> f;
      if (tag == "IN") {
        out.back().in.push_back(v);
      } else {
        out.back().out.push_back(v);
      }
    }
  }
  return out;
}

TEST(RegionOps, MatchesOriginal) {
  const std::vector<GoldenCase> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();
  int erode_cases = 0, dilate_cases = 0, false_cases = 0;

  for (const GoldenCase& c : cases) {
    const MapGrid grid(c.w, c.h);
    const std::size_t n = static_cast<std::size_t>(grid.side()) * grid.side();
    std::vector<PaintWorkCell> work(n);
    // 阶段 1 只对菱形内的格子写打包坐标；菱形外永远是 (0,0)。
    grid.ForEachCell([&](const CellCoord& p) {
      work[static_cast<std::size_t>(grid.WorkIndex(p))].coord = p;
    });

    RegionStub world(grid);
    for (const std::array<int, 7>& e : c.in) {
      const CellCoord p{e[0], e[1]};
      world.Seed(p, e[2], e[3], e[4]);
      PaintWorkCell& rec = work[static_cast<std::size_t>(grid.WorkIndex(p))];
      rec.blob_id = e[5];
      rec.queued = e[6];
    }

    Randomizer rng(1);
    RegionSet regions;
    RegionContext ctx;
    ctx.grid = grid;
    ctx.work = &work;
    ctx.world = &world;
    ctx.theater = &theater;
    ctx.rng = &rng;
    ctx.regions = &regions;
    ctx.map_w = c.w;
    ctx.map_h = c.h;

    rng.reset_call_count();
    bool ret = false;
    if (c.op == "ERODE") {
      ++erode_cases;
      ret = Erode(ctx, c.level30c, c.owner, c.rings, c.new_owner);
    } else {
      ASSERT_EQ(c.op, "DILATE") << "case " << c.name;
      ++dilate_cases;
      ret = Dilate(ctx, c.owner, c.rings, c.rect_x, c.rect_y, c.rect_w,
                   c.rect_h, c.flag != 0, c.cell_val);
    }
    if (!ret) ++false_cases;

    EXPECT_EQ(ret ? 1 : 0, c.ret) << "case " << c.name;
    // 两个算子里一条取数指令都没有
    EXPECT_EQ(rng.call_count(), 0) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().x, c.dummy.x) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().y, c.dummy.y) << "case " << c.name;

    int bad = 0;
    for (const std::array<int, 7>& e : c.out) {
      const CellCoord q{e[0], e[1]};
      const Cell got = world.Get(q);
      const PaintWorkCell& rec =
          work[static_cast<std::size_t>(grid.WorkIndex(q))];
      if (got.tile == e[2] && got.subtile == e[3] && got.level == e[4] &&
          rec.blob_id == e[5] && rec.queued == e[6]) {
        continue;
      }
      if (++bad <= 10) {
        ADD_FAILURE() << "case " << c.name << " cell (" << q.x << "," << q.y
                      << ") got tile=" << got.tile << " sub=" << got.subtile
                      << " lvl=" << static_cast<int>(got.level)
                      << " owner=" << rec.blob_id << " queued=" << rec.queued
                      << " want tile=" << e[2] << " sub=" << e[3]
                      << " lvl=" << e[4] << " owner=" << e[5]
                      << " queued=" << e[6];
      }
    }
    EXPECT_EQ(bad, 0) << "case " << c.name << ": " << bad << " cells differ";
  }

  // 覆盖面不许悄悄缩水：两个算子都要有用例，且失败出口至少被踩到一次
  EXPECT_GE(erode_cases, 16);
  EXPECT_GE(dilate_cases, 17);
  EXPECT_GE(false_cases, 5);
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
