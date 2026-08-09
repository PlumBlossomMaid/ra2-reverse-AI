// 河流生成器 0x59D510 River + 调度器 0x59C580 AddRiversAndLakes 的黄金对拍
//
// 基准由 code/randmap/dump_river_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/river_golden.txt。
// 文件中有两类记录：
//   CASE     —— 调用 River() 的单次结果
//   CASE_ARL —— 调用 AddRiversAndLakes() 的结果
// 每条记录包含逐格 IN/OUT 快照（tile/subtile/level/+0x38/+0x3C/+0x44/+0x4B）
// 以及 ret rng painted blob level dummy 等聚合量。
//
// ⚠️ 0x57A0C0 岸线平滑按 smooth= 钉成恒真/恒假，与原版 oracle 保持一致。
#include "rivers.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "scaled_random.h"
#include "tile_geometry_fixture.h"
#include "gtest/gtest.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

constexpr unsigned kSeed = 0x1234;

TheaterData Temperate() {
  TheaterData t;
  t.water_set           = 314;
  t.shore_pieces        = 89;
  t.green_tile          = 493;
  t.clear_to_green_lat  = 494;
  t.waterfall_east      = 539;
  t.waterfall_west      = 547;
  t.waterfall_north     = 543;
  t.waterfall_south     = 414;
  return t;
}

constexpr int kCellSlots = 0x40000;

int SlotOf(const CellCoord& c) {
  const int idx = (c.y << 9) + c.x;
  return (idx < 0 || idx >= kCellSlots) ? -1 : idx;
}

struct Cell {
  int tile = 0, subtile = 0;
  std::int8_t level = 0;
  int ramp = 0;
};

// RiverWorld 最小桩：同时满足 WaterBlobWorld + WaterfallWorld（含 TileWorld）
class RiverStub : public RiverWorld {
 public:
  RiverStub(const MapGrid& g, std::vector<PaintWorkCell>* work,
            const TheaterData& t)
      : grid_(g), work_(work), cells_(kCellSlots), exists_(kCellSlots, 0) {
    g.ForEachCell([&](const CellCoord& c) { exists_[SlotOf(c)] = 1; });
    AddGroup(t.waterfall_north, "wfnorth");
    AddGroup(t.waterfall_east,  "wfeast");
    AddGroup(t.waterfall_south, "wfsouth");
    AddGroup(t.waterfall_west,  "wfwest");
  }

  // ── RegionWorld ──
  bool CellExists(const CellCoord& c) const override {
    const int s = SlotOf(c);
    return s >= 0 && exists_[s] != 0;
  }
  void SetDummyCoord(const CellCoord& c) override { dummy_coord_ = c; }

  // ── WaterBlobWorld / TileWorld 共有 ──
  int IsoTile(const CellCoord& c) const override { return At(c).tile; }
  void SetIsoTile(const CellCoord& c, int v) override { Mut(c).tile = v; }
  void SetSubTile(const CellCoord& c, int v) override { Mut(c).subtile = v; }
  int Level(const CellCoord& c) const override {
    return static_cast<int>(At(c).level);
  }
  void SetLevel(const CellCoord& c, int v) override {
    Mut(c).level = static_cast<std::int8_t>(v);
  }

  // ── TileWorld ──
  int SubTile(const CellCoord& c) const override { return At(c).subtile; }
  void SetRamp(const CellCoord& c, int v) override { Mut(c).ramp = v; }
  int DummyIsoTile() const override { return dummy_.tile; }
  int DummySubTile() const override { return dummy_.subtile; }
  void SetDummyTile(int t, int st, int lv) override {
    dummy_.tile    = t;
    dummy_.subtile = st;
    dummy_.level   = static_cast<std::int8_t>(lv);
  }
  void SetDummyIsoTile(int t) override { dummy_.tile = t; }
  int WorkBlob(const CellCoord& c) const override {
    return (*work_)[WorkIdx(c)].blob_id;
  }
  void SetWorkBlob(const CellCoord& c, int v) override {
    (*work_)[WorkIdx(c)].blob_id = v;
  }

  // ── WaterfallWorld ──
  bool SmoothShoreline(int) override { return smooth_ret_; }
  const TileType* TileByIndex(int index) const override {
    for (const TileType& t : tiles_) {
      if (t.tile_number == index) return &t;
    }
    return nullptr;
  }

  void set_smooth_ret(bool v) { smooth_ret_ = v; }
  Cell Get(const CellCoord& c) const { return At(c); }
  void Seed(const CellCoord& c, int tile, int subtile, int level) {
    Cell& x = Mut(c);
    x.tile    = tile;
    x.subtile = subtile;
    x.level   = static_cast<std::int8_t>(level);
  }
  CellCoord dummy_coord() const { return dummy_coord_; }

 private:
  void AddGroup(int base, const char* group) {
    for (int k = 0; k < 4; ++k) {
      tiles_.push_back(fixture::MakeTile(
          base + k,
          fixture::RealGeometry().at(std::string(group) + ":" +
                                     std::to_string(k))));
    }
  }
  std::size_t WorkIdx(const CellCoord& c) const {
    long long i = static_cast<long long>(c.y) * grid_.side() + c.x;
    if (i < 0) i = 0;
    if (i >= static_cast<long long>(work_->size()))
      i = static_cast<long long>(work_->size()) - 1;
    return static_cast<std::size_t>(i);
  }
  const Cell& At(const CellCoord& c) const {
    const int s = SlotOf(c);
    return (s < 0 || !exists_[s]) ? dummy_ : cells_[s];
  }
  Cell& Mut(const CellCoord& c) {
    const int s = SlotOf(c);
    return (s < 0 || !exists_[s]) ? dummy_ : cells_[s];
  }

  MapGrid grid_;
  std::vector<PaintWorkCell>* work_;
  std::vector<Cell> cells_;
  std::vector<char> exists_;
  Cell dummy_;
  CellCoord dummy_coord_;
  std::vector<TileType> tiles_;
  bool smooth_ret_ = true;
};

// ── Golden data structures ──────────────────────────────────────────────────

struct RiverCase {
  std::string name;
  int w = 0, h = 0;
  int map_type = 3, water = 0, painted = 0, blob = 1, level = 4;
  int allow_wf = 0;
  CellCoord start{};
  double angle = 0.0;
  int is_branch = 0, smooth = 1;
  // expected outputs
  int ret = 0, rng = 0, total = 0, blob_out = 0, level_out = 0;
  CellCoord dummy{};
  std::vector<std::array<int, 9>> in;
  std::vector<std::array<int, 9>> out;
};

struct ArlCase {
  std::string name;
  int w = 0, h = 0;
  int map_type = 3, water = 0, painted = 0, blob = 1, level = 4;
  int allow_wf = 0, smooth = 1;
  // expected outputs
  int rng = 0, total = 0, blob_out = 0, level_out = 0;
  CellCoord dummy{};
  std::vector<std::array<int, 9>> in;
  std::vector<std::array<int, 9>> out;
};

// ── Helpers ─────────────────────────────────────────────────────────────────

int NamedInt(const std::string& tok, const char* key) {
  const std::size_t n = std::strlen(key);
  EXPECT_EQ(tok.compare(0, n, key), 0) << "expected " << key << " got " << tok;
  return std::atoi(tok.c_str() + n);
}

bool Has(const std::string& tok, const char* key) {
  return tok.compare(0, std::strlen(key), key) == 0;
}

struct GoldenData {
  std::vector<RiverCase> river;
  std::vector<ArlCase>   arl;
};

GoldenData LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/river_golden.txt";
  std::ifstream fin(path);
  EXPECT_TRUE(fin.good()) << "cannot open " << path;
  GoldenData data;
  // 0 = none, 1 = CASE (River), 2 = CASE_ARL
  int last_type = 0;
  std::string line;
  while (std::getline(fin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "CASE") {
      last_type = 1;
      RiverCase c;
      ss >> c.name >> c.w >> c.h;
      bool after_arrow = false;
      std::string tok;
      while (ss >> tok) {
        if (tok == "->") { after_arrow = true; continue; }
        if (Has(tok, "terrain=") || Has(tok, "owners=")) continue;
        if (!after_arrow) {
          if      (Has(tok, "maptype="))   c.map_type   = NamedInt(tok, "maptype=");
          else if (Has(tok, "water="))     c.water      = NamedInt(tok, "water=");
          else if (Has(tok, "painted="))   c.painted    = NamedInt(tok, "painted=");
          else if (Has(tok, "blob="))      c.blob       = NamedInt(tok, "blob=");
          else if (Has(tok, "level="))     c.level      = NamedInt(tok, "level=");
          else if (Has(tok, "allow_wf="))  c.allow_wf   = NamedInt(tok, "allow_wf=");
          else if (Has(tok, "start="))
            std::sscanf(tok.c_str(), "start=%d,%d", &c.start.x, &c.start.y);
          else if (Has(tok, "angle="))     c.angle      = std::atof(tok.c_str() + 6);
          else if (Has(tok, "is_branch=")) c.is_branch  = NamedInt(tok, "is_branch=");
          else if (Has(tok, "smooth="))    c.smooth     = NamedInt(tok, "smooth=");
        } else {
          if      (Has(tok, "ret="))    c.ret       = NamedInt(tok, "ret=");
          else if (Has(tok, "rng="))    c.rng       = NamedInt(tok, "rng=");
          else if (Has(tok, "total="))  c.total     = NamedInt(tok, "total=");
          else if (Has(tok, "blob="))   c.blob_out  = NamedInt(tok, "blob=");
          else if (Has(tok, "level="))  c.level_out = NamedInt(tok, "level=");
          else if (Has(tok, "dummy="))
            std::sscanf(tok.c_str(), "dummy=%d,%d", &c.dummy.x, &c.dummy.y);
        }
      }
      data.river.push_back(c);
    } else if (tag == "CASE_ARL") {
      last_type = 2;
      ArlCase c;
      ss >> c.name >> c.w >> c.h;
      bool after_arrow = false;
      std::string tok;
      while (ss >> tok) {
        if (tok == "->") { after_arrow = true; continue; }
        if (Has(tok, "terrain=") || Has(tok, "owners=")) continue;
        if (!after_arrow) {
          if      (Has(tok, "maptype="))  c.map_type  = NamedInt(tok, "maptype=");
          else if (Has(tok, "water="))    c.water     = NamedInt(tok, "water=");
          else if (Has(tok, "painted="))  c.painted   = NamedInt(tok, "painted=");
          else if (Has(tok, "blob="))     c.blob      = NamedInt(tok, "blob=");
          else if (Has(tok, "level="))    c.level     = NamedInt(tok, "level=");
          else if (Has(tok, "allow_wf=")) c.allow_wf  = NamedInt(tok, "allow_wf=");
          else if (Has(tok, "smooth="))   c.smooth    = NamedInt(tok, "smooth=");
        } else {
          if      (Has(tok, "rng="))    c.rng       = NamedInt(tok, "rng=");
          else if (Has(tok, "total="))  c.total     = NamedInt(tok, "total=");
          else if (Has(tok, "blob="))   c.blob_out  = NamedInt(tok, "blob=");
          else if (Has(tok, "level="))  c.level_out = NamedInt(tok, "level=");
          else if (Has(tok, "dummy="))
            std::sscanf(tok.c_str(), "dummy=%d,%d", &c.dummy.x, &c.dummy.y);
        }
      }
      data.arl.push_back(c);
    } else if (tag == "IN" || tag == "OUT") {
      std::array<int, 9> v{};
      for (int& f : v) ss >> f;
      if (last_type == 1 && !data.river.empty()) {
        if (tag == "IN") data.river.back().in.push_back(v);
        else             data.river.back().out.push_back(v);
      } else if (last_type == 2 && !data.arl.empty()) {
        if (tag == "IN") data.arl.back().in.push_back(v);
        else             data.arl.back().out.push_back(v);
      }
    }
  }
  return data;
}

// ── 公共：建 RegionContext ───────────────────────────────────────────────────

static RegionContext MakeCtx(const MapGrid& grid,
                             std::vector<PaintWorkCell>& work,
                             RiverStub& world,
                             const TheaterData& theater,
                             Randomizer& rng,
                             NormalDeviate& normal,
                             RegionSet& regions,
                             int w, int h) {
  RegionContext ctx;
  ctx.grid    = grid;
  ctx.work    = &work;
  // diamond: RiverWorld -> WaterBlobWorld -> RegionWorld; pick one path
  ctx.world   = static_cast<WaterBlobWorld*>(&world);
  ctx.theater = &theater;
  ctx.rng     = &rng;
  ctx.regions = &regions;
  ctx.normal  = &normal;
  ctx.map_w   = w;
  ctx.map_h   = h;
  return ctx;
}

static void SeedWorld(RiverStub& world, std::vector<PaintWorkCell>& work,
                      const MapGrid& grid,
                      const std::vector<std::array<int, 9>>& rows) {
  for (const auto& e : rows) {
    const CellCoord p{static_cast<std::int16_t>(e[0]),
                      static_cast<std::int16_t>(e[1])};
    world.Seed(p, e[2], e[3], e[4]);
    PaintWorkCell& rec =
        work[static_cast<std::size_t>(grid.WorkIndex(p))];
    rec.blob_id = e[5];
    rec.queued  = e[6];
    rec.flag44  = e[7] != 0;
    rec.flag4b  = e[8] != 0;
  }
}

static int CheckCells(const std::string& name,
                      const RiverStub& world,
                      const std::vector<PaintWorkCell>& work,
                      const MapGrid& grid,
                      const std::vector<std::array<int, 9>>& rows) {
  int bad = 0;
  for (const auto& e : rows) {
    const CellCoord q{static_cast<std::int16_t>(e[0]),
                      static_cast<std::int16_t>(e[1])};
    const Cell got = world.Get(q);
    const PaintWorkCell& rec =
        work[static_cast<std::size_t>(grid.WorkIndex(q))];
    const bool ok = (got.tile == e[2] && got.subtile == e[3] &&
                     static_cast<int>(got.level) == e[4] &&
                     rec.blob_id == e[5] && rec.queued == e[6] &&
                     (rec.flag44 ? 1 : 0) == (e[7] != 0 ? 1 : 0) &&
                     (rec.flag4b ? 1 : 0) == (e[8] != 0 ? 1 : 0));
    if (!ok && ++bad <= 10) {
      ADD_FAILURE()
          << "case " << name << " cell (" << q.x << "," << q.y << ")"
          << " got tile=" << got.tile << " sub=" << got.subtile
          << " lvl=" << static_cast<int>(got.level)
          << " owner=" << rec.blob_id << " queued=" << rec.queued
          << " allow=" << rec.flag44 << " f4b=" << rec.flag4b
          << " want tile=" << e[2] << " sub=" << e[3]
          << " lvl=" << e[4] << " owner=" << e[5]
          << " queued=" << e[6] << " allow=" << e[7] << " f4b=" << e[8];
    }
  }
  return bad;
}

// ── TEST: River() ────────────────────────────────────────────────────────────

TEST(River, MatchesOriginal) {
  const GoldenData golden = LoadGolden();
  ASSERT_FALSE(golden.river.empty());
  const TheaterData theater = Temperate();
  int false_cases = 0, fixed_cases = 0;

  for (const RiverCase& c : golden.river) {
    const MapGrid grid(c.w, c.h);
    const std::size_t n =
        static_cast<std::size_t>(grid.side()) * grid.side();
    std::vector<PaintWorkCell> work(n);
    grid.ForEachCell([&](const CellCoord& p) {
      work[static_cast<std::size_t>(grid.WorkIndex(p))].coord = p;
    });

    RiverStub world(grid, &work, theater);
    world.set_smooth_ret(c.smooth != 0);
    SeedWorld(world, work, grid, c.in);

    Randomizer rng(kSeed);
    NormalDeviate normal(rng);
    RegionSet regions;
    RegionContext ctx = MakeCtx(grid, work, world, theater,
                                rng, normal, regions, c.w, c.h);

    RiverParams p;
    p.map_type      = c.map_type;
    p.water_amount  = c.water;
    p.painted_total = c.painted;
    p.blob_id       = c.blob;
    p.cell_level    = c.level;
    p.allow_waterfall = (c.allow_wf != 0);

    rng.reset_call_count();
    bool ret = false;
    {
      FpuChopScope fpu;
      ret = River(ctx, world, p, c.start, c.angle,
                  static_cast<bool>(c.is_branch));
    }
    if (!ret) ++false_cases;
    if (c.start.x != 0 || c.start.y != 0) ++fixed_cases;

    EXPECT_EQ(ret ? 1 : 0, c.ret)      << "case " << c.name;
    EXPECT_EQ(rng.call_count(), c.rng) << "case " << c.name;
    EXPECT_EQ(p.painted_total, c.total)<< "case " << c.name;
    EXPECT_EQ(p.blob_id,    c.blob_out)<< "case " << c.name;
    EXPECT_EQ(p.cell_level, c.level_out)<< "case " << c.name;
    EXPECT_EQ(world.dummy_coord().x, c.dummy.x) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().y, c.dummy.y) << "case " << c.name;

    const int bad = CheckCells(c.name, world, work, grid, c.out);
    EXPECT_EQ(bad, 0) << "case " << c.name << ": " << bad << " cells differ";
  }

  EXPECT_GE(golden.river.size(), 12u);
  EXPECT_GE(false_cases, 3);
  EXPECT_GE(fixed_cases, 3);
}

// ── TEST: AddRiversAndLakes() ────────────────────────────────────────────────

TEST(AddRiversAndLakes, MatchesOriginal) {
  const GoldenData golden = LoadGolden();
  ASSERT_FALSE(golden.arl.empty());
  const TheaterData theater = Temperate();

  for (const ArlCase& c : golden.arl) {
    const MapGrid grid(c.w, c.h);
    const std::size_t n =
        static_cast<std::size_t>(grid.side()) * grid.side();
    std::vector<PaintWorkCell> work(n);
    grid.ForEachCell([&](const CellCoord& p) {
      work[static_cast<std::size_t>(grid.WorkIndex(p))].coord = p;
    });

    RiverStub world(grid, &work, theater);
    world.set_smooth_ret(c.smooth != 0);
    SeedWorld(world, work, grid, c.in);

    Randomizer rng(kSeed);
    NormalDeviate normal(rng);
    RegionSet regions;
    RegionContext ctx = MakeCtx(grid, work, world, theater,
                                rng, normal, regions, c.w, c.h);

    RiverParams p;
    p.map_type        = c.map_type;
    p.water_amount    = c.water;
    p.painted_total   = c.painted;
    p.blob_id         = c.blob;
    p.cell_level      = c.level;
    p.allow_waterfall = (c.allow_wf != 0);

    rng.reset_call_count();
    {
      FpuChopScope fpu;
      AddRiversAndLakes(ctx, world, p);
    }

    EXPECT_EQ(rng.call_count(), c.rng)  << "case " << c.name;
    EXPECT_EQ(p.painted_total, c.total) << "case " << c.name;
    EXPECT_EQ(p.blob_id,    c.blob_out) << "case " << c.name;
    EXPECT_EQ(p.cell_level, c.level_out)<< "case " << c.name;
    EXPECT_EQ(world.dummy_coord().x, c.dummy.x) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().y, c.dummy.y) << "case " << c.name;

    const int bad = CheckCells(c.name, world, work, grid, c.out);
    EXPECT_EQ(bad, 0) << "case " << c.name << ": " << bad << " cells differ";
  }

  EXPECT_GE(golden.arl.size(), 6u);
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
