// 阶段 2 调度器（原版 0x59A6C0）的黄金对拍
//
// 这是目前最"整"的一条对拍链：一次调用把铺水 → 投放策略 → 涂块核心 →
// 岸线平滑四遍 → 掩码 → 贴砖器全跑一遍，比对整张地图的终态。
// 基准由 code/randmap/dump_stage_water_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/stage_water_golden.txt。比五样：
//   1) 随机数调用次数（整条链的取数顺序）
//   2) 收尾时的块编号 MapSeedClass +0x308（失败也跳号）
//   3) 共享哑元 0xABDC50 的 +0x38（收尾刷绿会写它，写完后续出图邻格全跳过）
//   4) 工作数组头一条记录的 +0x38 / +0x3C（0x59A7A5 那遍整片写 -1）
//   5) 菱形内每一格的终态 (tile, subtile, level, workBlob)
//
// ⚠️ 42 块岸砖用的是**合成尺寸**，两套剖面，规则与 dump_shore_smooth_golden.py
// 的 shore_tile_size 逐字一致；换成真实 .tmp 尺寸要重取基准。
// ⚠️ MapType 0 在小图上每块都铺不出岸线、整块回滚，终态全是水——那是原版
// 真实行为（基准里 mt0-allwater 一例专门盯住它），不是桩失效。
#include "stage_water.h"
#include "water_refine.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

constexpr int kWaterSet = 314;      // temperate WaterSet
constexpr int kShorePieces = 89;    // temperate ShorePieces
constexpr int kGreenTile = 493;     // temperate GreenTile

TheaterData Temperate() {
  TheaterData t;
  t.water_set = kWaterSet;
  t.shore_pieces = kShorePieces;
  t.green_tile = kGreenTile;
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
};

constexpr int kCellSlots = 0x40000;   // 格子指针数组按 (Y << 9) + X 索引

int SlotOf(const CellCoord& c) {
  const int idx = (c.y << 9) + c.x;
  return (idx < 0 || idx >= kCellSlots) ? -1 : idx;
}

// 桩把 PaintWorld 与 SmoothWorld 两侧接到同一份格子/工作数组上——这一点很要紧：
// 原版里 PaintWorkCell.blob_id 与 TileWorld::WorkBlob 读写的是同一个 +0x38。
class StageStub : public WaterStageWorld {
 public:
  StageStub(const MapGrid& g, const TheaterData& theater, Randomizer& rng,
            std::vector<PaintWorkCell>& work, bool unit_tiles)
      : grid_(g), theater_(theater), rng_(rng), work_(work),
        cells_(kCellSlots), exists_(kCellSlots, 0),
        cache_(work.size(), -1) {
    g.ForEachCell([&](const CellCoord& c) { exists_[SlotOf(c)] = 1; });
    for (int k = 0; k < 0x2A; ++k) {
      TileType t;
      t.tile_number = theater.shore_pieces + k;
      t.width = unit_tiles ? 1 : 1 + (k % 2);
      t.height = unit_tiles ? 1 : 1 + ((k / 2) % 2);
      t.cells.assign(static_cast<std::size_t>(t.width) * t.height,
                     TileSubCell{true, 0, 0});
      tiles_[t.tile_number] = t;
    }
    // WaterSet 的 0x0E 块：0x59C795 会以 TileTypes[WaterSet + shape] 取砖，
    // 尺寸/坡度的合成规则与 dump_water_refine_golden.py 逐字一致。
    for (int k = 0; k < 0x0E; ++k) {
      TileType t;
      t.tile_number = theater.water_set + k;
      t.width = k < 8 ? 2 : 1;
      t.height = k < 8 ? 2 : 1;
      for (int j = 0; j < t.width * t.height; ++j) {
        t.cells.push_back(TileSubCell{true, 0, 1 + (k * 5 + j * 3) % 17});
      }
      tiles_[t.tile_number] = t;
    }
  }

  // ── PaintWorld ∩ ShoreWorld ∩ TileWorld：一份实现覆盖三个基类 ──
  int IsoTile(const CellCoord& c) const override {
    const int s = SlotOf(c);
    return (s < 0 || !exists_[s]) ? ShoreWorld::kNoCell : cells_[s].tile;
  }
  void SetIsoTile(const CellCoord& c, int v) override { Mut(c).tile = v; }

  // ── PaintWorld ──
  bool IsPaintable(const CellCoord&) const override { return true; }
  void SetCellSubTile(const CellCoord& c, int v) override { Mut(c).subtile = v; }
  void SetCellLevel(const CellCoord& c, int v) override { Mut(c).level = v; }
  // 0x59C433：涂块收尾调 Shoreline(blob, **1**)
  bool SmoothShoreline(int blob_id) override {
    return ShorelineSmooth(*this, grid_, theater_, rng_, blob_id, true);
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

  int WorkBlob(const CellCoord& c) const override {
    return work_[WorkIdx(c)].blob_id;
  }
  void SetWorkBlob(const CellCoord& c, int v) override {
    work_[WorkIdx(c)].blob_id = v;
  }

  // ── SmoothWorld ──
  void SetMaskCache(const CellCoord& c, int v) override { cache_[WorkIdx(c)] = v; }
  void ResetMaskCaches() override { cache_.assign(cache_.size(), -1); }
  int Level(const CellCoord& c) const override { return At(c).level; }
  CellCoord DummyCoord() const override { return dummy_coord_; }
  const TileType* TileByIndex(int index) const override {
    const auto it = tiles_.find(index);
    return it == tiles_.end() ? nullptr : &it->second;
  }

  // ── WaterStageWorld ──
  void ResetWorkOwners() override {
    for (PaintWorkCell& w : work_) {
      w.blob_id = -1;
      w.queued = -1;
    }
  }

  Cell Get(const CellCoord& c) const { return At(c); }
  int DummyTile() const { return dummy_.tile; }

 private:
  std::size_t WorkIdx(const CellCoord& c) const {
    long long i = static_cast<long long>(c.y) * grid_.side() + c.x;
    if (i < 0) i = 0;
    if (i >= static_cast<long long>(work_.size())) i = work_.size() - 1;
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
  const TheaterData& theater_;
  Randomizer& rng_;
  std::vector<PaintWorkCell>& work_;
  std::vector<Cell> cells_;
  std::vector<char> exists_;
  std::vector<int> cache_;
  std::map<int, TileType> tiles_;
  Cell dummy_;
  CellCoord dummy_coord_;
};

struct GoldenCase {
  std::string name;
  int w = 0, h = 0;
  unsigned seed = 0;
  int players = 0, water = 0, map_type = 0;
  Rect usable;
  bool unit_tiles = false;
  long long rng = 0;
  int stamp = 0, dummy = 0, work38 = 0, work3c = 0;
  std::vector<std::array<int, 6>> cells;   // x y tile subtile level workBlob
};

int NamedInt(const std::string& tok, const char* key) {
  const std::size_t n = std::strlen(key);
  EXPECT_EQ(tok.compare(0, n, key), 0) << "expected " << key << " got " << tok;
  return std::atoi(tok.c_str() + n);
}

Rect NamedRect(const std::string& tok) {
  Rect r;
  std::sscanf(tok.c_str(), "usable=%d,%d,%d,%d", &r.x, &r.y, &r.w, &r.h);
  return r;
}

std::vector<GoldenCase> LoadGolden() {
  const std::string path = std::string(RA2_GOLDEN_DIR) + "/stage_water_golden.txt";
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
      std::string seedtok, ptok, wtok, mttok, utok, tilestok, arrow,
          rngtok, stamptok, dummytok, w38tok, w3ctok;
      ss >> c.name >> c.w >> c.h >> seedtok >> ptok >> wtok >> mttok >> utok
         >> tilestok >> arrow >> rngtok >> stamptok >> dummytok >> w38tok
         >> w3ctok;
      c.seed = static_cast<unsigned>(
          std::strtoul(seedtok.c_str() + std::strlen("seed=0x"), nullptr, 16));
      c.players = NamedInt(ptok, "players=");
      c.water = NamedInt(wtok, "water=");
      c.map_type = NamedInt(mttok, "mt=");
      c.usable = NamedRect(utok);
      c.unit_tiles = tilestok == "tiles=unit";
      c.rng = NamedInt(rngtok, "rng=");
      c.stamp = NamedInt(stamptok, "stamp=");
      c.dummy = NamedInt(dummytok, "dummy=");
      c.work38 = NamedInt(w38tok, "work38=");
      c.work3c = NamedInt(w3ctok, "work3c=");
      out.push_back(c);
    } else if (tag == "CELL") {
      std::array<int, 6> v{};
      for (int& f : v) ss >> f;
      out.back().cells.push_back(v);
    }
  }
  return out;
}

TEST(StageWater, MatchesOriginal) {
  const std::vector<GoldenCase> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();

  for (const GoldenCase& c : cases) {
    const MapGrid grid(c.w, c.h);
    const std::size_t n = static_cast<std::size_t>(grid.side()) * grid.side();
    std::vector<PaintWorkCell> work(n);
    Randomizer rng(c.seed);
    StageStub world(grid, theater, rng, work, c.unit_tiles);
    NormalDeviate normal(rng);
    int stamp = 0;

    PaintContext ctx;
    ctx.grid = grid;
    ctx.full_bounds = Rect{-1, -1, -1, -1};
    ctx.work = &work;
    ctx.theater = &theater;
    ctx.rng = &rng;
    ctx.normal = &normal;
    ctx.blob_stamp = &stamp;
    ctx.cell_level = 4;

    PlaceParams p;
    p.num_players = c.players;
    p.water_amount = c.water;
    p.usable = c.usable;

    rng.reset_call_count();
    {
      FpuChopScope chop;
      SeedWater(ctx, world, p, c.map_type);
    }

    EXPECT_EQ(rng.call_count(), c.rng) << "case " << c.name;
    EXPECT_EQ(stamp, c.stamp) << "case " << c.name;
    EXPECT_EQ(world.DummyTile(), c.dummy) << "case " << c.name;
    EXPECT_EQ(work[0].blob_id, c.work38) << "case " << c.name;
    EXPECT_EQ(work[0].queued, c.work3c) << "case " << c.name;

    int bad = 0;
    for (const std::array<int, 6>& e : c.cells) {
      const CellCoord q{e[0], e[1]};
      const Cell got = world.Get(q);
      const int blob = world.WorkBlob(q);
      if (got.tile == e[2] && got.subtile == e[3] && got.level == e[4] &&
          blob == e[5]) {
        continue;
      }
      if (++bad <= 10) {
        ADD_FAILURE() << "case " << c.name << " cell (" << q.x << "," << q.y
                      << ") got tile=" << got.tile << " sub=" << got.subtile
                      << " lvl=" << got.level << " blob=" << blob
                      << " want tile=" << e[2] << " sub=" << e[3]
                      << " lvl=" << e[4] << " blob=" << e[5];
      }
    }
    EXPECT_EQ(bad, 0) << "case " << c.name << ": " << bad << " cells differ";
  }
}


// ── 水面细化 0x59C630（接在阶段 2 之后）─────────────────────────────────
//
// 基准 memory/data/rmg/water_refine_golden.txt 是同一批用例、同一批种子，
// 先跑 0x59A6C0 再跑 0x59C630 的连贯产物（rngstage 一栏与 stage_water 基准
// 的 rng 一栏逐例相同，互为交叉验证）。每格多比一个 CellClass +0x11C。
struct RefineCase {
  std::string name;
  int w = 0, h = 0;
  unsigned seed = 0;
  int players = 0, water = 0, map_type = 0;
  Rect usable;
  bool unit_tiles = false;
  long long rng_stage = 0, rng_total = 0;
  int dummy = 0;
  std::vector<std::array<int, 7>> cells;   // x y tile subtile level ramp blob
};

std::vector<RefineCase> LoadRefineGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/water_refine_golden.txt";
  std::ifstream in(path);
  EXPECT_TRUE(in.good()) << "cannot open " << path;
  std::vector<RefineCase> out;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '') line.pop_back();
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "CASE") {
      RefineCase c;
      std::string seedtok, ptok, wtok, mttok, utok, tilestok, arrow,
          stagetok, totaltok, dummytok;
      ss >> c.name >> c.w >> c.h >> seedtok >> ptok >> wtok >> mttok >> utok
         >> tilestok >> arrow >> stagetok >> totaltok >> dummytok;
      c.seed = static_cast<unsigned>(
          std::strtoul(seedtok.c_str() + std::strlen("seed=0x"), nullptr, 16));
      c.players = NamedInt(ptok, "players=");
      c.water = NamedInt(wtok, "water=");
      c.map_type = NamedInt(mttok, "mt=");
      c.usable = NamedRect(utok);
      c.unit_tiles = tilestok == "tiles=unit";
      c.rng_stage = NamedInt(stagetok, "rngstage=");
      c.rng_total = NamedInt(totaltok, "rngtotal=");
      c.dummy = NamedInt(dummytok, "dummy=");
      out.push_back(c);
    } else if (tag == "CELL") {
      std::array<int, 7> v{};
      for (int& f : v) ss >> f;
      out.back().cells.push_back(v);
    }
  }
  return out;
}

TEST(WaterRefine, MatchesOriginal) {
  const std::vector<RefineCase> cases = LoadRefineGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();

  for (const RefineCase& c : cases) {
    const MapGrid grid(c.w, c.h);
    const std::size_t n = static_cast<std::size_t>(grid.side()) * grid.side();
    std::vector<PaintWorkCell> work(n);
    Randomizer rng(c.seed);
    StageStub world(grid, theater, rng, work, c.unit_tiles);
    NormalDeviate normal(rng);
    int stamp = 0;

    PaintContext ctx;
    ctx.grid = grid;
    ctx.full_bounds = Rect{-1, -1, -1, -1};
    ctx.work = &work;
    ctx.theater = &theater;
    ctx.rng = &rng;
    ctx.normal = &normal;
    ctx.blob_stamp = &stamp;
    ctx.cell_level = 4;

    PlaceParams p;
    p.num_players = c.players;
    p.water_amount = c.water;
    p.usable = c.usable;

    rng.reset_call_count();
    {
      FpuChopScope chop;
      SeedWater(ctx, world, p, c.map_type);
      EXPECT_EQ(rng.call_count(), c.rng_stage) << "case " << c.name;
      RefineWaterTiles(world, grid, theater, rng);
    }

    EXPECT_EQ(rng.call_count(), c.rng_total) << "case " << c.name;
    EXPECT_EQ(world.DummyTile(), c.dummy) << "case " << c.name;

    int bad = 0;
    for (const std::array<int, 7>& e : c.cells) {
      const CellCoord q{e[0], e[1]};
      const Cell got = world.Get(q);
      const int blob = world.WorkBlob(q);
      if (got.tile == e[2] && got.subtile == e[3] && got.level == e[4] &&
          got.ramp == e[5] && blob == e[6]) {
        continue;
      }
      if (++bad <= 10) {
        ADD_FAILURE() << "case " << c.name << " cell (" << q.x << "," << q.y
                      << ") got tile=" << got.tile << " sub=" << got.subtile
                      << " lvl=" << got.level << " ramp=" << got.ramp
                      << " blob=" << blob << " want tile=" << e[2]
                      << " sub=" << e[3] << " lvl=" << e[4] << " ramp=" << e[5]
                      << " blob=" << e[6];
      }
    }
    EXPECT_EQ(bad, 0) << "case " << c.name << ": " << bad << " cells differ";
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
