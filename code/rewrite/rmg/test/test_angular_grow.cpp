// 0x5A08D0 AngularGrow 的黄金对拍
//
// 基准由 code/randmap/dump_angular_grow_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/angular_grow_golden.txt。每个用例给一张输入
// （IN 行：tile / subtile / level / +0x38 归属 / +0x3C 戳 / +0x4B），
// 调一次之后比四样：
//   1) 返回值 —— 0 就是"扩散途中撞上了第三方地盘"
//   2) 随机数调用次数 —— 每入堆一格取一次，外加迭代次数那一次拒绝采样
//   3) 每格终态 (tile, subtile, level, +0x38, +0x3C, +0x4B)
//   4) 共享哑元 0xABDC50 的 +0x24
//
// 用例里几条是专门盯着容易被"简化掉"的行为造的：
//   g-base0-full / g-base-pi / g-base-halfpi / g-base-3halfpi / g-base0-xnz
//        —— 基准角那两组顺序执行的 if，第二组会无条件盖掉第一组
//   g-w2-noflush / g-w2-flush —— 迭代次数为 0 时堆里全是种子，收尾遍才见分晓
//   g-flush-lake              —— 收尾遍认领时不查砖，水砖也照收
//   g-wneg                    —— weight 为负 → 上限为负 → 那条无符号拒绝
//                                采样要转好几百次才出得来
//   g-wall / g-solid          —— 邻格无主但砖不是未定砖：既不入堆也不算失败
#include "regions.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "fast_math.h"
#include "gtest/gtest.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

// 生成脚本里 OpsOracle 固定用的种子（dump_region_ops_golden.py）
constexpr unsigned kSeed = 0x1234;

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

constexpr int kCellSlots = 0x40000;

int SlotOf(const CellCoord& c) {
  const int idx = (c.y << 9) + c.x;
  return (idx < 0 || idx >= kCellSlots) ? -1 : idx;
}

struct Cell {
  int tile = 0;
  int subtile = 0;
  std::int8_t level = 0;
};

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
  int blob = 0;
  float weight = 0.0f;
  Rect rect;
  CellCoord origin;
  int flush = 0;
  int ret = 0, rng = 0;
  CellCoord dummy;
  std::vector<std::array<int, 8>> in;   // x y tile sub lvl owner queued flag4b
  std::vector<std::array<int, 8>> out;
};

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
      std::string(RA2_GOLDEN_DIR) + "/angular_grow_golden.txt";
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
      ss >> c.name >> c.w >> c.h;
      std::string tok;
      while (ss >> tok) {
        if (Has(tok, "owners=") || Has(tok, "terrain=")) {
          continue;
        } else if (Has(tok, "blob=")) {
          c.blob = NamedInt(tok, "blob=");
        } else if (Has(tok, "weight=")) {
          c.weight = static_cast<float>(std::atof(tok.c_str() + 7));
        } else if (Has(tok, "rect=")) {
          std::sscanf(tok.c_str(), "rect=%d,%d,%d,%d", &c.rect.x, &c.rect.y,
                      &c.rect.w, &c.rect.h);
        } else if (Has(tok, "origin=")) {
          std::sscanf(tok.c_str(), "origin=%d,%d", &c.origin.x, &c.origin.y);
        } else if (Has(tok, "flush=")) {
          c.flush = NamedInt(tok, "flush=");
        } else if (Has(tok, "ret=")) {
          c.ret = NamedInt(tok, "ret=");
        } else if (Has(tok, "rng=")) {
          c.rng = NamedInt(tok, "rng=");
        } else if (Has(tok, "dummy=")) {
          std::sscanf(tok.c_str(), "dummy=%d,%d", &c.dummy.x, &c.dummy.y);
        }
      }
      out.push_back(c);
    } else if (tag == "IN" || tag == "OUT") {
      std::array<int, 8> v{};
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

TEST(AngularGrow, MatchesOriginal) {
  const std::vector<GoldenCase> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();
  FpuChopScope fpu;
  int false_cases = 0, flush_cases = 0;

  for (const GoldenCase& c : cases) {
    const MapGrid grid(c.w, c.h);
    const std::size_t n = static_cast<std::size_t>(grid.side()) * grid.side();
    std::vector<PaintWorkCell> work(n);
    grid.ForEachCell([&](const CellCoord& p) {
      work[static_cast<std::size_t>(grid.WorkIndex(p))].coord = p;
    });

    RegionStub world(grid);
    for (const std::array<int, 8>& e : c.in) {
      const CellCoord p{e[0], e[1]};
      world.Seed(p, e[2], e[3], e[4]);
      PaintWorkCell& rec = work[static_cast<std::size_t>(grid.WorkIndex(p))];
      rec.blob_id = e[5];
      rec.queued = e[6];
      rec.flag4b = e[7] != 0;
    }

    Randomizer rng(kSeed);
    NormalDeviate normal(rng);
    RegionSet regions;
    RegionContext ctx;
    ctx.grid = grid;
    ctx.work = &work;
    ctx.world = &world;
    ctx.theater = &theater;
    ctx.rng = &rng;
    ctx.normal = &normal;
    ctx.regions = &regions;
    ctx.map_w = c.w;
    ctx.map_h = c.h;

    rng.reset_call_count();
    const bool ret =
        AngularGrow(ctx, c.blob, c.weight, c.rect, c.origin, c.flush != 0);
    if (!ret) ++false_cases;
    if (c.flush) ++flush_cases;

    EXPECT_EQ(ret ? 1 : 0, c.ret) << "case " << c.name;
    EXPECT_EQ(rng.call_count(), c.rng) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().x, c.dummy.x) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().y, c.dummy.y) << "case " << c.name;

    int bad = 0;
    for (const std::array<int, 8>& e : c.out) {
      const CellCoord q{e[0], e[1]};
      const Cell got = world.Get(q);
      const PaintWorkCell& rec =
          work[static_cast<std::size_t>(grid.WorkIndex(q))];
      if (got.tile == e[2] && got.subtile == e[3] && got.level == e[4] &&
          rec.blob_id == e[5] && rec.queued == e[6] &&
          (rec.flag4b ? 1 : 0) == e[7]) {
        continue;
      }
      if (++bad <= 10) {
        ADD_FAILURE() << "case " << c.name << " cell (" << q.x << "," << q.y
                      << ") got tile=" << got.tile << " sub=" << got.subtile
                      << " lvl=" << static_cast<int>(got.level)
                      << " owner=" << rec.blob_id << " queued=" << rec.queued
                      << " f4b=" << (rec.flag4b ? 1 : 0)
                      << " want tile=" << e[2] << " sub=" << e[3]
                      << " lvl=" << e[4] << " owner=" << e[5]
                      << " queued=" << e[6] << " f4b=" << e[7];
      }
    }
    EXPECT_EQ(bad, 0) << "case " << c.name << ": " << bad << " cells differ";
  }

  // 覆盖面不许悄悄缩水
  EXPECT_GE(static_cast<int>(cases.size()), 27);
  EXPECT_GE(false_cases, 3);
  EXPECT_GE(flush_cases, 4);
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
