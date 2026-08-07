// 阶段 3「Init regions」（原版 0x598960 中段）的黄金对拍
//
// 基准由 code/randmap/dump_regions_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/regions_golden.txt。每个用例给一张**输入地形**
// （IN 行：tile / subtile / level / 工作记录 +0x4B），跑完整条四步流程之后比四样：
//   1) 随机数调用次数 —— 本阶段唯一的取数点 0x58CAF9 有没有在该抽的时候抽
//   2) 区域表 —— 个数、顺序，以及每个区域的 id / 格数 / 层 / wet / 种子 / +0x1A
//   3) 编号计数器 [0xABED14] 的终值 —— 它和区域个数**不一定相等**：
//      并区分支返回时不消耗编号，而重置分支会把已用的编号作废
//   4) 每格终态 (tile, subtile, level, +0x38 归属, +0x3C 入队标记)
//   5) 共享哑元 0xABDC50 的 +0x24 —— GetBorderCells 每碰一个界外邻格就写一次，
//      终值等于最后一次界外访问的坐标，是"遍历顺序"的一个廉价指纹
//
// ⚠️ 基准里 syn-dry-* 三例是特意构造的：全图砖号 1（既不是水/岸也不是绿），
// CreateRegions 一个区域都建不出来，于是 Finalize 起的第一个种子 id == 0，
// 正好落进那条唯一会抽随机数的分支。没有这几例的话取数点根本跑不到。
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

// 最小可用的格子世界。要紧的一条：格子不存在时所有读写都落到**同一份**
// 哑元上（原版 0xABDC50），而不是返回哨兵值或静默丢弃。
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
  std::string name;
  int w = 0, h = 0;
  unsigned seed = 0;
  int map_type = 0;
  long long rng = 0;
  int regions = 0, next_id = 0;
  CellCoord dummy;
  std::vector<std::array<int, 6>> in;     // x y tile subtile level flag4b
  std::vector<std::array<int, 7>> out;    // x y tile subtile level owner queued
  std::vector<std::array<int, 7>> reg;    // id cnt level wet sx sy finalized
};

int NamedInt(const std::string& tok, const char* key) {
  const std::size_t n = std::strlen(key);
  EXPECT_EQ(tok.compare(0, n, key), 0) << "expected " << key << " got " << tok;
  return std::atoi(tok.c_str() + n);
}

std::vector<GoldenCase> LoadGolden() {
  const std::string path = std::string(RA2_GOLDEN_DIR) + "/regions_golden.txt";
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
      std::string seedtok, ptok, wtok, mttok, utok, tilestok, terrtok, f4btok,
          arrow, rngtok, regtok, nidtok, dummytok;
      ss >> c.name >> c.w >> c.h >> seedtok >> ptok >> wtok >> mttok >> utok
         >> tilestok >> terrtok >> f4btok >> arrow >> rngtok >> regtok
         >> nidtok >> dummytok;
      c.seed = static_cast<unsigned>(
          std::strtoul(seedtok.c_str() + std::strlen("seed=0x"), nullptr, 16));
      c.map_type = NamedInt(mttok, "mt=");
      c.rng = NamedInt(rngtok, "rng=");
      c.regions = NamedInt(regtok, "regions=");
      c.next_id = NamedInt(nidtok, "nextid=");
      std::sscanf(dummytok.c_str(), "dummy=%d,%d", &c.dummy.x, &c.dummy.y);
      out.push_back(c);
    } else if (tag == "IN") {
      std::array<int, 6> v{};
      for (int& f : v) ss >> f;
      out.back().in.push_back(v);
    } else if (tag == "OUT") {
      std::array<int, 7> v{};
      for (int& f : v) ss >> f;
      out.back().out.push_back(v);
    } else if (tag == "REG") {
      std::array<int, 7> v{};
      for (int& f : v) ss >> f;
      out.back().reg.push_back(v);
    }
  }
  return out;
}

TEST(Regions, MatchesOriginal) {
  const std::vector<GoldenCase> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();

  for (const GoldenCase& c : cases) {
    const MapGrid grid(c.w, c.h);
    const std::size_t n = static_cast<std::size_t>(grid.side()) * grid.side();
    std::vector<PaintWorkCell> work(n);
    // 阶段 1 只对菱形内的格子写打包坐标；菱形外永远是 (0,0)。
    grid.ForEachCell([&](const CellCoord& p) {
      work[static_cast<std::size_t>(grid.WorkIndex(p))].coord = p;
    });

    RegionStub world(grid);
    for (const std::array<int, 6>& e : c.in) {
      const CellCoord p{e[0], e[1]};
      world.Seed(p, e[2], e[3], e[4]);
      work[static_cast<std::size_t>(grid.WorkIndex(p))].flag4b = (e[5] != 0);
    }

    Randomizer rng(c.seed);
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
    ctx.fallback_level = 4;      // MapSeedClass +0x30C = [0xABE2E4]

    rng.reset_call_count();
    {
      FpuChopScope chop;         // 取数点 0x58CB19 走 _ftol（截断）
      InitRegions(ctx, c.map_type);
    }

    EXPECT_EQ(rng.call_count(), c.rng) << "case " << c.name;
    EXPECT_EQ(regions.Count(), c.regions) << "case " << c.name;
    EXPECT_EQ(regions.next_id(), c.next_id) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().x, c.dummy.x) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().y, c.dummy.y) << "case " << c.name;

    ASSERT_EQ(static_cast<int>(c.reg.size()), c.regions) << "case " << c.name;
    for (std::size_t i = 0; i < c.reg.size() &&
                            i < static_cast<std::size_t>(regions.Count());
         ++i) {
      const RegionClass& r = *regions.At(static_cast<int>(i));
      const std::array<int, 7>& e = c.reg[i];
      EXPECT_EQ(r.id, e[0]) << "case " << c.name << " region " << i;
      EXPECT_EQ(r.cell_count, e[1]) << "case " << c.name << " region " << i;
      EXPECT_EQ(r.level, e[2]) << "case " << c.name << " region " << i;
      EXPECT_EQ(r.wet ? 1 : 0, e[3]) << "case " << c.name << " region " << i;
      EXPECT_EQ(r.seed.x, e[4]) << "case " << c.name << " region " << i;
      EXPECT_EQ(r.seed.y, e[5]) << "case " << c.name << " region " << i;
      EXPECT_EQ(r.finalized ? 1 : 0, e[6])
          << "case " << c.name << " region " << i;
    }

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
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
