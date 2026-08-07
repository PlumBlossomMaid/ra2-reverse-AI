// 岸线平滑（原版 0x57A0C0，四遍扫全图）的黄金对拍
//
// 基准由 code/randmap/dump_shore_smooth_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/shore_smooth_golden.txt。比三样：
//   1) 返回值（false = 这块地铺不出岸线，调用方要整块回滚）
//   2) 随机数调用次数（第 3/4 遍每格无条件抽一次 PickRange(0,5)，
//      次数错一次后面所有阶段的随机流就全错位）
//   3) 菱形内每一格的终态 (tile, subtile, level, workBlob)
//
// 42 块岸砖的尺寸有三套剖面（CASE 行的 tiles= 列），定义见 tile_geometry_fixture.h：
//   mix / unit 是合成的（压分支用），real 是真实 .tmp 文件头（2x3 / 3x2 / 6x4 / 9x5
//   这些尺寸合成规则给不出，一块砖能盖住 45 格）。三套都要留，理由见那个头文件。
#include "shore_smooth.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "scaled_random.h"
#include "tile_geometry_fixture.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

constexpr int kWaterSet = 314;      // temperate WaterSet
constexpr int kShorePieces = 89;    // temperate ShorePieces

TheaterData Temperate() {
  TheaterData t;
  t.water_set = kWaterSet;
  t.shore_pieces = kShorePieces;
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

struct Cell {
  int tile = 0;
  int subtile = 0;
  int level = 0;
  int ramp = 0;      // CellClass +0x11C
  int blob = 0;
};

// 格子指针数组按 (Y << 9) + X 索引，与原版 0x5657A0 一致；下标越界 = 没这一格。
constexpr int kCellSlots = 0x40000;

int SlotOf(const CellCoord& c) {
  const int idx = (c.y << 9) + c.x;
  return (idx < 0 || idx >= kCellSlots) ? -1 : idx;
}

class SmoothStub : public SmoothWorld {
 public:
  SmoothStub(const MapGrid& g, int water, int shore,
             const std::string& profile)
      : grid_(g), water_(water), shore_(shore),
        cells_(kCellSlots), exists_(kCellSlots, 0),
        cache_(static_cast<std::size_t>(g.side()) * g.side(), -1),
        blob_(static_cast<std::size_t>(g.side()) * g.side(), 0) {
    g.ForEachCell([&](const CellCoord& c) { exists_[SlotOf(c)] = 1; });
    // 42 块岸砖，尺寸剖面与 oracle 的 tile_profiles.shore_spec 同源
    for (int k = 0; k < 0x2A; ++k) {
      tiles_.push_back(
          fixture::MakeTile(shore_ + k, fixture::ShoreGeom(k, profile)));
    }
  }

  // ── ShoreWorld ∩ TileWorld：同名同签名，一份实现同时覆盖两个基类 ──
  int IsoTile(const CellCoord& c) const override {
    const int s = SlotOf(c);
    return (s < 0 || !exists_[s]) ? ShoreWorld::kNoCell : cells_[s].tile;
  }

  // ── ShoreWorld ──
  bool WorkValid(const CellCoord&) const override { return true; }
  int MaskCache(const CellCoord& c) const override { return cache_[WorkIdx(c)]; }

  // ── TileWorld ──
  bool CellExists(const CellCoord& c) const override {
    const int s = SlotOf(c);
    return s >= 0 && exists_[s] != 0;
  }
  int SubTile(const CellCoord& c) const override { return At(c).subtile; }
  void SetIsoTile(const CellCoord& c, int v) override { Mut(c).tile = v; }
  void SetSubTile(const CellCoord& c, int v) override { Mut(c).subtile = v; }
  void SetLevel(const CellCoord& c, int v) override { Mut(c).level = v; }
  void SetRamp(const CellCoord& c, int v) override { Mut(c).ramp = v; }

  void SetDummyCoord(const CellCoord& c) override { dummy_coord_ = c; }
  int DummyIsoTile() const override { return dummy_.tile; }
  int DummySubTile() const override { return dummy_.subtile; }
  void SetDummyTile(int t, int st, int lv) override {
    dummy_.tile = t;
    dummy_.subtile = st;
    dummy_.level = lv;
  }
  void SetDummyIsoTile(int t) override { dummy_.tile = t; }

  int WorkBlob(const CellCoord& c) const override { return blob_[WorkIdx(c)]; }
  void SetWorkBlob(const CellCoord& c, int v) override { blob_[WorkIdx(c)] = v; }

  // ── SmoothWorld ──
  void SetMaskCache(const CellCoord& c, int v) override { cache_[WorkIdx(c)] = v; }
  void ResetMaskCaches() override { cache_.assign(cache_.size(), -1); }
  int Level(const CellCoord& c) const override { return At(c).level; }
  CellCoord DummyCoord() const override { return dummy_coord_; }
  const TileType* TileByIndex(int index) const override {
    const int k = index - shore_;
    if (k < 0 || k >= static_cast<int>(tiles_.size())) return nullptr;
    return &tiles_[k];
  }

  void Seed(const CellCoord& c, int tile) { Mut(c).tile = tile; }
  Cell Get(const CellCoord& c) const {
    Cell r = At(c);
    r.blob = blob_[WorkIdx(c)];
    return r;
  }

 private:
  std::size_t WorkIdx(const CellCoord& c) const {
    // 工作数组按 Y * side + X 索引（0x5A00C0/0x5A0090）。基准里的坐标一律在
    // 菱形内或紧邻，落不出 side²；越界就夹住，免得测试自己先崩。
    long long i = static_cast<long long>(c.y) * grid_.side() + c.x;
    if (i < 0) i = 0;
    if (i >= static_cast<long long>(blob_.size())) i = blob_.size() - 1;
    return static_cast<std::size_t>(i);
  }
  const Cell& At(const CellCoord& c) const {
    const int s = SlotOf(c);
    return s < 0 ? dummy_ : cells_[s];
  }
  Cell& Mut(const CellCoord& c) {
    const int s = SlotOf(c);
    return s < 0 ? dummy_ : cells_[s];
  }

  MapGrid grid_;
  int water_, shore_;
  std::vector<Cell> cells_;
  std::vector<char> exists_;
  std::vector<int> cache_;
  std::vector<int> blob_;
  std::vector<TileType> tiles_;
  Cell dummy_;
  CellCoord dummy_coord_;
};

struct GoldenCase {
  std::string name;
  int w = 0, h = 0;
  unsigned seed = 0;
  int blob = 0;
  bool flag = false;
  std::string profile;       // CASE 行的 tiles= 列：mix / unit / real
  int ret = 0;
  long long rng = 0;
  std::vector<CellCoord> land;
  std::vector<std::array<int, 6>> cells;   // x y tile subtile level blob
};

int NamedInt(const std::string& tok, const char* key) {
  const std::size_t n = std::strlen(key);
  EXPECT_EQ(tok.compare(0, n, key), 0) << "expected " << key << " got " << tok;
  return std::atoi(tok.c_str() + n);
}

std::vector<GoldenCase> LoadGolden() {
  const std::string path = std::string(RA2_GOLDEN_DIR) + "/shore_smooth_golden.txt";
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
      std::string seedtok, kind, blobtok, flagtok, tilestok,
          arrow, rettok, rngtok, landtok;
      ss >> c.name >> c.w >> c.h >> seedtok >> kind >> blobtok >> flagtok
         >> tilestok >> arrow >> rettok >> rngtok >> landtok;
      c.seed = static_cast<unsigned>(
          std::strtoul(seedtok.c_str() + std::strlen("seed=0x"), nullptr, 16));
      c.blob = NamedInt(blobtok, "blob=");
      c.flag = NamedInt(flagtok, "flag=") != 0;
      c.profile = tilestok.substr(std::strlen("tiles="));
      c.ret = NamedInt(rettok, "ret=");
      c.rng = NamedInt(rngtok, "rng=");
      out.push_back(c);
    } else if (tag == "LAND") {
      std::string tok;
      while (ss >> tok) {
        const std::size_t colon = tok.find(':');
        out.back().land.push_back(
            CellCoord{std::atoi(tok.c_str()),
                      std::atoi(tok.c_str() + colon + 1)});
      }
    } else if (tag == "CELL") {
      std::array<int, 6> v{};
      for (int& f : v) ss >> f;
      out.back().cells.push_back(v);
    }
  }
  return out;
}

TEST(ShoreSmooth, MatchesOriginal) {
  const std::vector<GoldenCase> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();

  for (const GoldenCase& c : cases) {
    const MapGrid grid(c.w, c.h);
    SmoothStub world(grid, kWaterSet, kShorePieces, c.profile);
    // 初态：陆地 = 未定砖 0，其余菱形内格子 = 水面组基
    grid.ForEachCell([&](const CellCoord& p) { world.Seed(p, kWaterSet); });
    for (const CellCoord& p : c.land) world.Seed(p, 0);

    Randomizer rng(c.seed);
    rng.reset_call_count();
    bool ret;
    {
      FpuChopScope chop;
      ret = ShorelineSmooth(world, grid, theater, rng, c.blob, c.flag);
    }
    EXPECT_EQ(ret ? 1 : 0, c.ret) << "case " << c.name;
    EXPECT_EQ(rng.call_count(), c.rng) << "case " << c.name;

    int bad = 0;
    for (const std::array<int, 6>& e : c.cells) {
      const CellCoord p{e[0], e[1]};
      const Cell got = world.Get(p);
      if (got.tile == e[2] && got.subtile == e[3] && got.level == e[4] &&
          got.blob == e[5]) {
        continue;
      }
      if (++bad <= 10) {
        ADD_FAILURE() << "case " << c.name << " cell (" << p.x << "," << p.y
                      << ") got tile=" << got.tile << " sub=" << got.subtile
                      << " lvl=" << got.level << " blob=" << got.blob
                      << " want tile=" << e[2] << " sub=" << e[3]
                      << " lvl=" << e[4] << " blob=" << e[5];
      }
    }
    EXPECT_EQ(bad, 0) << "case " << c.name << ": " << bad << " cells differ";
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
