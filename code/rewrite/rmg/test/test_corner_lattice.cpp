// 阶段 9「造山」整条链（原版 0x5A35F0）的黄金对拍
//
// 基准由 code/randmap/dump_hills_golden.py 用 Unicorn 跑原版机器码取得，存档
// memory/data/rmg/hills_golden.txt。每个用例给一张「菱形内每格的
// (砖号, 子格号, 层高, 坡面, overlay, object)」与工作记录 +0x08 的高度场位型，
// 跑完之后比三样：
//   1) 0x6B3850 入口处（即 ⑤ 之前）整张角点格网里所有 dirty 的角点
//      —— 这一项把 ③ 建格网、④ 抬升、0x6B3A80 的递归连带调整、
//         [0,180] 越界回退、锁角回滚全都钉住了
//   2) 每一格最终的 (砖号, 子格号, 层高, 坡面)；基准没列的格子必须原样不动
//   3) 共享哑元 0xABDC50 的同四项与它的坐标 +0x24 —— §九 收尾遍探到菱形外时
//      会改写它，这条副作用也要一致
//
// ⚠️ 基准全部 Ruggedness = 0：0x5A2F50 在 0x5A2FA8 直接返回，一个随机数都不抽，
// 连尾巴那趟「整表四舍五入」也一起跳过（取整循环在那个 if 里面，见
// stage-hills.md §二）。高度场因此保持脚本摆进去的原值，可能是非整数。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "corner_lattice.h"
#include "gtest/gtest.h"
#include "height_field.h"   // MarkHeightAnchors / GenerateHeightField

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

// temperate 一列，与基准脚本的 THEATER 表逐项一致
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
  t.clear_tile = 0;
  return t;
}

struct CellState {
  int tile = 0;
  int subtile = 0;   // +0x11A，无符号字节
  int level = 0;     // +0x11B，**有符号**字节
  int ramp = 0;      // +0x11C，无符号字节
  int overlay = -1;  // +0x44
  int object = 0;    // +0xE4
};

// 只实现造山碰得到的那些字段。字节宽度照原版截断：+0x11A/+0x11C 是无符号字节，
// +0x11B 是有符号字节，+0x38 是 dword。
class HillStub : public HillWorld {
 public:
  explicit HillStub(const std::set<int>& nonmorph) : nonmorph_(nonmorph) {}

  bool CellExists(const CellCoord& c) const override {
    return cells_.count(Key(c)) != 0;
  }
  int IsoTile(const CellCoord& c) const override { return Get(c).tile; }
  int SubTile(const CellCoord& c) const override { return Get(c).subtile; }
  int Level(const CellCoord& c) const override { return Get(c).level; }
  int Ramp(const CellCoord& c) const override { return Get(c).ramp; }
  int Overlay(const CellCoord& c) const override { return Get(c).overlay; }
  bool HasObject(const CellCoord& c) const override {
    return Get(c).object != 0;
  }
  bool TileMorphable(int tile) const override {
    // 0x6B2564 起：tile < [0xA8ED38] 且 tile != 0xFFFF 才查表
    if (tile == 0xFFFF || tile >= 900) return true;
    return nonmorph_.count(tile) == 0;
  }

  void SetIsoTile(const CellCoord& c, int v) override { Ref(c).tile = v; }
  void SetSubTile(const CellCoord& c, int v) override {
    Ref(c).subtile = static_cast<unsigned char>(v);
  }
  void SetLevel(const CellCoord& c, int v) override {
    Ref(c).level = static_cast<signed char>(v);
  }
  void SetRamp(const CellCoord& c, int v) override {
    Ref(c).ramp = static_cast<unsigned char>(v);
  }

  void SetDummyCoord(const CellCoord& c) override { dummy_coord_ = c; }
  CellCoord DummyCoord() const override { return dummy_coord_; }
  int DummyIsoTile() const override { return dummy_.tile; }
  int DummySubTile() const override { return dummy_.subtile; }
  int DummyLevel() const override { return dummy_.level; }
  int DummyRamp() const override { return dummy_.ramp; }
  void SetDummyLevel(int v) override {
    dummy_.level = static_cast<signed char>(v);
  }
  void SetDummyRamp(int v) override {
    dummy_.ramp = static_cast<unsigned char>(v);
  }
  void SetDummyTile(int tile, int subtile, int level) override {
    dummy_.tile = tile;
    dummy_.subtile = static_cast<unsigned char>(subtile);
    dummy_.level = static_cast<signed char>(level);
  }
  void SetDummyIsoTile(int tile) override { dummy_.tile = tile; }

  int WorkBlob(const CellCoord&) const override { return 0; }
  void SetWorkBlob(const CellCoord&, int) override {}

  void Preset(const CellCoord& c, const CellState& s) { cells_[Key(c)] = s; }
  const CellState& CellAt(const CellCoord& c) const { return Get(c); }
  const CellState& Dummy() const { return dummy_; }

 private:
  static long long Key(const CellCoord& c) { return c.y * 100000LL + c.x; }
  const CellState& Get(const CellCoord& c) const {
    std::map<long long, CellState>::const_iterator it = cells_.find(Key(c));
    if (it == cells_.end()) return dummy_;
    return it->second;
  }
  CellState& Ref(const CellCoord& c) { return cells_[Key(c)]; }

  std::map<long long, CellState> cells_;
  CellState dummy_;   // 0xABDC50，脚本把它整块清零后只把 +0x44 写成 -1
  CellCoord dummy_coord_;
  std::set<int> nonmorph_;
};

struct LatEntry {
  int x = 0, y = 0, height = 0;
  bool locked = false;
};

struct Case {
  std::string name;
  int w = 0, h = 0;
  std::set<int> nonmorph;
  std::vector<CellCoord> coords;
  std::vector<CellState> in;
  std::vector<double> height;
  std::map<long long, CellState> out;   // 只有变化过的格子
  CellState dummy;
  CellCoord dummy_coord;
  std::vector<LatEntry> lat;
};

double BitsToDouble(const std::string& hex) {
  unsigned long long bits = 0;
  for (std::size_t i = 0; i < hex.size(); ++i) {
    const char ch = hex[i];
    int v = 0;
    if (ch >= 48 && ch <= 57) v = ch - 48;            // 0-9
    else if (ch >= 65 && ch <= 70) v = ch - 65 + 10;  // A-F
    else if (ch >= 97 && ch <= 102) v = ch - 97 + 10; // a-f
    bits = (bits << 4) | static_cast<unsigned long long>(v);
  }
  double d = 0.0;
  std::memcpy(&d, &bits, 8);
  return d;
}

long long CellKey(int x, int y) { return y * 100000LL + x; }

std::vector<Case> LoadGolden() {
  const std::string path = std::string(RA2_GOLDEN_DIR) + "/hills_golden.txt";
  std::ifstream f(path.c_str());
  EXPECT_TRUE(f.good()) << "打不开基准档 " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == 35) continue;   // 注释行以 # 开头
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
    if (key == "MORPH") {
      int t;
      is >> t;
      c.nonmorph.insert(t);
    } else if (key == "IN") {
      int x, y;
      CellState s;
      std::string bits;
      is >> x >> y >> s.tile >> s.subtile >> s.level >> s.ramp >> s.overlay >>
          s.object >> bits;
      c.coords.push_back(CellCoord{x, y});
      c.in.push_back(s);
      c.height.push_back(BitsToDouble(bits));
    } else if (key == "OUT") {
      int x, y;
      CellState s;
      is >> x >> y >> s.tile >> s.subtile >> s.level >> s.ramp;
      c.out[CellKey(x, y)] = s;
    } else if (key == "DUMMY") {
      int x, y;
      is >> c.dummy.tile >> c.dummy.subtile >> c.dummy.level >> c.dummy.ramp >>
          x >> y;
      c.dummy_coord = CellCoord{x, y};
    } else if (key == "LAT") {
      LatEntry e;
      int lk;
      is >> e.x >> e.y >> e.height >> lk;
      e.locked = lk != 0;
      c.lat.push_back(e);
    }
  }
  return cases;
}

TEST(CornerLattice, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name + " " + std::to_string(c.w) + "x" +
                 std::to_string(c.h));

    HillStub world(c.nonmorph);
    for (std::size_t k = 0; k < c.coords.size(); ++k) {
      world.Preset(c.coords[k], c.in[k]);
    }

    HillContext ctx;
    ctx.grid = MapGrid(c.w, c.h);
    ctx.world = &world;
    ctx.theater = &theater;
    ctx.ruggedness = 0;
    // Ruggedness 0 -> 0x5A2F50 在 0x5A2FA8 直接返回，这两个都碰不到
    Randomizer rng(1);
    NormalDeviate nd(rng);
    ctx.nd = &nd;

    const int side = ctx.grid.side();
    std::vector<PaintWorkCell> work(static_cast<std::size_t>(side) * side);
    ctx.work = &work;
    for (std::size_t k = 0; k < c.coords.size(); ++k) {
      PaintWorkCell& w =
          work[static_cast<std::size_t>(ctx.grid.WorkIndex(c.coords[k]))];
      w.coord = c.coords[k];
      w.height = c.height[k];
    }

    // (1)~(4)，然后在 (5) 销毁格网之前采格网快照（基准是在 0x6B3850 入口采的）
    MarkHeightAnchors(world, ctx.grid, theater, work);
    GenerateHeightField(ctx.grid, work, ctx.ruggedness, nd);
    BuildCornerLattice(ctx);
    RaiseAllCells(ctx);

    std::vector<LatEntry> got;
    for (int y = 0; y < ctx.lattice.rows; ++y) {
      for (int x = 0; x < ctx.lattice.stride; ++x) {
        const CornerEntry& e = ctx.lattice.At(x, y);
        if (!e.dirty) continue;
        LatEntry g;
        g.x = x;
        g.y = y;
        g.height = e.height;
        g.locked = e.locked;
        got.push_back(g);
      }
    }
    ASSERT_EQ(c.lat.size(), got.size()) << "dirty 角点的个数不对";
    for (std::size_t k = 0; k < got.size(); ++k) {
      ASSERT_EQ(c.lat[k].x, got[k].x) << "第 " << k << " 个 dirty 角点的 X";
      ASSERT_EQ(c.lat[k].y, got[k].y) << "第 " << k << " 个 dirty 角点的 Y";
      ASSERT_EQ(c.lat[k].height, got[k].height)
          << "角点 (" << got[k].x << "," << got[k].y << ") 的高度";
      ASSERT_EQ(c.lat[k].locked, got[k].locked)
          << "角点 (" << got[k].x << "," << got[k].y << ") 的锁标记";
    }

    ApplyLatticeToCells(ctx);
    FlattenRampBlocks(ctx);

    for (std::size_t k = 0; k < c.coords.size(); ++k) {
      const CellCoord& p = c.coords[k];
      CellState want = c.in[k];
      std::map<long long, CellState>::const_iterator it =
          c.out.find(CellKey(p.x, p.y));
      if (it != c.out.end()) want = it->second;
      const CellState& g = world.CellAt(p);
      ASSERT_EQ(want.tile, g.tile) << "格 (" << p.x << "," << p.y << ") 的砖号";
      ASSERT_EQ(want.subtile, g.subtile)
          << "格 (" << p.x << "," << p.y << ") 的子格号";
      ASSERT_EQ(want.level, g.level) << "格 (" << p.x << "," << p.y << ") 的层高";
      ASSERT_EQ(want.ramp, g.ramp) << "格 (" << p.x << "," << p.y << ") 的坡面";
    }

    const CellState& d = world.Dummy();
    EXPECT_EQ(c.dummy.tile, d.tile) << "哑元砖号";
    EXPECT_EQ(c.dummy.subtile, d.subtile) << "哑元子格号";
    EXPECT_EQ(c.dummy.level, d.level) << "哑元层高";
    EXPECT_EQ(c.dummy.ramp, d.ramp) << "哑元坡面";
    EXPECT_EQ(c.dummy_coord.x, world.DummyCoord().x) << "哑元坐标 X";
    EXPECT_EQ(c.dummy_coord.y, world.DummyCoord().y) << "哑元坐标 Y";
  }
}

// RampTable 的 19 行：0x83FF18 那张表的形状本身就是复刻物，单独钉一遍。
TEST(CornerLattice, RampTableShape) {
  for (int k = 0; k < 19; ++k) {
    for (int i = 0; i < 4; ++i) {
      EXPECT_EQ(0, kRampTable[k][i] % 15) << "第 " << k << " 行第 " << i << " 项";
      EXPECT_GE(kRampTable[k][i], 0);
      EXPECT_LE(kRampTable[k][i], 30);
    }
  }
  // 第 0 行指向 .bss 0xB0B6DC，全 exe 无写入点，恒为全零
  for (int i = 0; i < 4; ++i) EXPECT_EQ(0, kRampTable[0][i]);
  // 13~16 行带 30，要求 mx-mn = 30 >= 0x10，而 0x6B3850 在那之前就 return 了，
  // 所以这四行在 RMG 路径上永远查不到
  for (int k = 13; k <= 16; ++k) {
    int mx = 0, mn = 1000;
    for (int i = 0; i < 4; ++i) {
      if (kRampTable[k][i] > mx) mx = kRampTable[k][i];
      if (kRampTable[k][i] < mn) mn = kRampTable[k][i];
    }
    EXPECT_GE(mx - mn, 0x10) << "第 " << k << " 行本该是够不到的";
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
