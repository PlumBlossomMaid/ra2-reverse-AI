// 绿地扩张（原版 0x59B740，阶段 5 的前半段）的黄金对拍
//
// 基准由 code/randmap/dump_green_spread_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/green_spread_golden.txt。每个用例给一张输入地形
// （IN 行）与共享哑元的进场状态，跑完之后比三样：
//   1) 随机数调用次数 —— 轮数 = 前沿长度 / 3（上限 1000），错一次全盘错位
//   2) 菱形内每一格的终态 (tile, subtile, level)
//      —— 0x59B740 只写 +0x38，后两样必须原样不动
//   3) 共享哑元 0xABDC50 的 +0x38 与 +0x24
//      —— 出图的邻格全退化成它；它一旦被涂成 GreenTile 就不再「未定砖」，
//      后面所有出图邻格都会被跳过。这个副作用能改变产物，必须比。
//
// ⚠️ 合成用例的底色是**未定砖**（tile 0）而不是 tile 1：前沿只收 0 与 0xFFFF
// （0x486380），铺 tile 1 的话一格都收不进来，整条循环退化成空转。
#include "recalc.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "scaled_random.h"   // FpuChopScope

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
  return t;
}

constexpr int kCellSlots = 0x40000;   // 格子指针数组按 (Y << 9) + X 索引

int SlotOf(const CellCoord& c) {
  const int idx = (c.y << 9) + c.x;
  return (idx < 0 || idx >= kCellSlots) ? -1 : idx;
}

struct Cell {
  int tile = 0;
  int subtile = 0;
  int level = 0;
  int ramp = 0;
};

// 只为这一段准备的最小世界。要紧的一条：格子不存在时所有读写都落到
// **同一份**哑元上（原版 0xABDC50），它的 +0x38 与 +0x24 都是可观测状态。
class GreenStub : public SmoothWorld {
 public:
  explicit GreenStub(const MapGrid& g)
      : cells_(kCellSlots), exists_(kCellSlots, 0) {
    g.ForEachCell([&](const CellCoord& c) { exists_[SlotOf(c)] = 1; });
  }

  // ── ShoreWorld ∩ TileWorld（一份实现覆盖两个基类）──
  int IsoTile(const CellCoord& c) const override {
    const int s = SlotOf(c);
    return (s < 0 || !exists_[s]) ? ShoreWorld::kNoCell : cells_[s].tile;
  }
  void SetIsoTile(const CellCoord& c, int v) override { Mut(c).tile = v; }

  // ── ShoreWorld ──（0x59B740 用不到，给最简实现）
  bool WorkValid(const CellCoord&) const override { return true; }
  int MaskCache(const CellCoord&) const override { return -1; }

  // ── TileWorld ──
  bool CellExists(const CellCoord& c) const override {
    const int s = SlotOf(c);
    return s >= 0 && exists_[s] != 0;
  }
  int SubTile(const CellCoord& c) const override { return At(c).subtile; }
  void SetSubTile(const CellCoord& c, int v) override { Mut(c).subtile = v; }
  void SetLevel(const CellCoord& c, int v) override { Mut(c).level = v; }
  void SetRamp(const CellCoord& c, int v) override { Mut(c).ramp = v; }

  void SetDummyCoord(const CellCoord& c) override { dummy_coord_ = c; }
  CellCoord DummyCoord() const override { return dummy_coord_; }
  int DummyIsoTile() const override { return dummy_.tile; }
  int DummySubTile() const override { return dummy_.subtile; }
  void SetDummyTile(int t, int st, int lv) override {
    dummy_.tile = t;
    dummy_.subtile = st;
    dummy_.level = lv;
  }
  void SetDummyIsoTile(int t) override { dummy_.tile = t; }

  int WorkBlob(const CellCoord&) const override { return -1; }
  void SetWorkBlob(const CellCoord&, int) override {}

  // ── SmoothWorld ──
  void SetMaskCache(const CellCoord&, int) override {}
  void ResetMaskCaches() override {}
  int Level(const CellCoord& c) const override { return At(c).level; }
  const TileType* TileByIndex(int) const override { return nullptr; }

  // ── 测试用 ──
  Cell Get(const CellCoord& c) const { return At(c); }
  void Seed(const CellCoord& c, int tile, int subtile, int level) {
    Cell& x = Mut(c);
    x.tile = tile;
    x.subtile = subtile;
    x.level = level;
  }
  void SeedDummy(int tile, const CellCoord& c) {
    dummy_.tile = tile;
    dummy_coord_ = c;
  }
  int dummy_tile() const { return dummy_.tile; }

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
  std::string name;
  int w = 0, h = 0;
  unsigned seed = 0;
  long long rng = 0;
  int din_tile = 0, dout_tile = 0;
  CellCoord din, dout;
  std::vector<std::array<int, 5>> in;    // x y tile subtile level
  std::vector<std::array<int, 5>> out;
};

int NamedInt(const std::string& tok, const char* key) {
  const std::size_t n = std::strlen(key);
  EXPECT_EQ(tok.compare(0, n, key), 0) << "expected " << key << " got " << tok;
  return std::atoi(tok.c_str() + n);
}

std::vector<GoldenCase> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/green_spread_golden.txt";
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
      std::string seedtok, mttok, tilestok, terrtok, arrow, rngtok, dintok,
          douttok;
      ss >> c.name >> c.w >> c.h >> seedtok >> mttok >> tilestok >> terrtok
         >> arrow >> rngtok >> dintok >> douttok;
      c.seed = static_cast<unsigned>(
          std::strtoul(seedtok.c_str() + std::strlen("seed=0x"), nullptr, 16));
      c.rng = NamedInt(rngtok, "rng=");
      std::sscanf(dintok.c_str(), "din=%d,%d,%d", &c.din_tile, &c.din.x,
                  &c.din.y);
      std::sscanf(douttok.c_str(), "dout=%d,%d,%d", &c.dout_tile, &c.dout.x,
                  &c.dout.y);
      out.push_back(c);
    } else if (tag == "IN") {
      std::array<int, 5> v{};
      for (int& f : v) ss >> f;
      out.back().in.push_back(v);
    } else if (tag == "OUT") {
      std::array<int, 5> v{};
      for (int& f : v) ss >> f;
      out.back().out.push_back(v);
    }
  }
  return out;
}

TEST(GreenSpread, MatchesOriginal) {
  const std::vector<GoldenCase> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();

  for (const GoldenCase& c : cases) {
    const MapGrid grid(c.w, c.h);
    GreenStub world(grid);
    for (const std::array<int, 5>& e : c.in) {
      world.Seed(CellCoord{e[0], e[1]}, e[2], e[3], e[4]);
    }
    world.SeedDummy(c.din_tile, c.din);

    Randomizer rng(c.seed);
    rng.reset_call_count();
    {
      FpuChopScope chop;   // 取数点 0x59B862 走 _ftol（截断）
      SpreadGreen(world, grid, theater, rng);
    }

    EXPECT_EQ(rng.call_count(), c.rng) << "case " << c.name;
    EXPECT_EQ(world.dummy_tile(), c.dout_tile) << "case " << c.name;
    EXPECT_EQ(world.DummyCoord().x, c.dout.x) << "case " << c.name;
    EXPECT_EQ(world.DummyCoord().y, c.dout.y) << "case " << c.name;

    int bad = 0;
    for (const std::array<int, 5>& e : c.out) {
      const CellCoord q{e[0], e[1]};
      const Cell got = world.Get(q);
      if (got.tile == e[2] && got.subtile == e[3] && got.level == e[4]) continue;
      if (++bad <= 10) {
        ADD_FAILURE() << "case " << c.name << " cell (" << q.x << "," << q.y
                      << ") got tile=" << got.tile << " sub=" << got.subtile
                      << " lvl=" << got.level << " want tile=" << e[2]
                      << " sub=" << e[3] << " lvl=" << e[4];
      }
    }
    EXPECT_EQ(bad, 0) << "case " << c.name << ": " << bad << " cells differ";
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
