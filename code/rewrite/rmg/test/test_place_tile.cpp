// 贴砖器（原版 0x57B440）的黄金对拍
//
// 基准由 code/randmap/dump_place_tile_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/place_tile_golden.txt。比三样：返回值、出参 ok、
// 以及被写过的每一格 (tile, subtile, level, workBlob)。
//
// theater 砖号取 temperate 那一列（memory/data/rmg/tileset_starts.txt），
// 与基准脚本的 THEATER 表逐项一致。
#include "place_tile.h"

#include <map>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace ra2 {
namespace rmg {
namespace {

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
  return t;
}

struct CellState {
  int tile = 0;
  int subtile = 0;
  int level = 0;
  int blob = 0;
};

class StubWorld : public TileWorld {
 public:
  explicit StubWorld(const MapGrid& g) {
    g.ForEachCell([&](const CellCoord& c) { cells_[Key(c)]; });
  }

  bool CellExists(const CellCoord& c) const override {
    return cells_.count(Key(c)) != 0;
  }
  int IsoTile(const CellCoord& c) const override { return At(c).tile; }
  int SubTile(const CellCoord& c) const override { return At(c).subtile; }
  void SetIsoTile(const CellCoord& c, int v) override { cells_[Key(c)].tile = v; }
  void SetSubTile(const CellCoord& c, int v) override {
    cells_[Key(c)].subtile = v;
  }
  void SetLevel(const CellCoord& c, int v) override { cells_[Key(c)].level = v; }

  void SetDummyCoord(const CellCoord&) override {}
  int DummyIsoTile() const override { return dummy_.tile; }
  int DummySubTile() const override { return dummy_.subtile; }
  void SetDummyTile(int t, int st, int lv) override {
    dummy_.tile = t;
    dummy_.subtile = st;
    dummy_.level = lv;
  }
  void SetDummyIsoTile(int t) override { dummy_.tile = t; }

  int WorkBlob(const CellCoord& c) const override { return At(c).blob; }
  void SetWorkBlob(const CellCoord& c, int v) override {
    cells_[Key(c)].blob = v;
  }

  void Preset(int x, int y, int tile, int subtile, int level) {
    CellState& s = cells_[Key(CellCoord{x, y})];
    s.tile = tile;
    s.subtile = subtile;
    s.level = level;
  }
  void PresetBlob(int x, int y, int b) { cells_[Key(CellCoord{x, y})].blob = b; }
  CellState Get(int x, int y) const { return At(CellCoord{x, y}); }

 private:
  static long long Key(const CellCoord& c) { return c.y * 100000LL + c.x; }
  CellState At(const CellCoord& c) const {
    auto it = cells_.find(Key(c));
    return it == cells_.end() ? CellState() : it->second;
  }

  mutable std::map<long long, CellState> cells_;
  CellState dummy_;
};

TileType MakeTile(int number, int w, int h,
                  const std::vector<std::pair<bool, int>>& blocks) {
  TileType t;
  t.tile_number = number;
  t.width = w;
  t.height = h;
  for (const auto& b : blocks) {
    TileSubCell c;
    c.present = b.first;
    c.height = b.second;
    t.cells.push_back(c);
  }
  return t;
}

struct Outcome {
  bool ret;
  bool ok;
};

Outcome Place(StubWorld& w, const MapGrid& g, const TileType& tile,
              CellCoord at, int lo, int hi, int level, int blob, bool notrack) {
  PlaceTileParams p;
  p.allow_lo = lo;
  p.allow_hi = hi;
  p.base_level = level;
  p.blob_id = blob;
  p.no_blob_tracking = notrack;
  bool ok = true;
  p.ok = &ok;
  const bool ret = PlaceTile(w, g, Temperate(), tile, at, p);
  return Outcome{ret, ok};
}

const MapGrid kGrid(20, 20);
const std::vector<std::pair<bool, int>> kFlat4 = {
    {true, 0}, {true, 0}, {true, 0}, {true, 0}};

// ── 用例逐条对应 memory/data/rmg/place_tile_golden.txt ──

TEST(PlaceTile, PlainTwoByTwo) {
  StubWorld w(kGrid);
  const TileType t = MakeTile(89, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_TRUE(r.ret);
  EXPECT_TRUE(r.ok);
  // 子格号 k = y*width + x，层高 = 子块高度 + base_level，归属被认领
  EXPECT_EQ(w.Get(10, 20).tile, 89);
  EXPECT_EQ(w.Get(10, 20).subtile, 0);
  EXPECT_EQ(w.Get(11, 20).subtile, 1);
  EXPECT_EQ(w.Get(10, 21).subtile, 2);
  EXPECT_EQ(w.Get(11, 21).subtile, 3);
  EXPECT_EQ(w.Get(11, 21).level, 3);
  EXPECT_EQ(w.Get(11, 21).blob, 5);
}

TEST(PlaceTile, HoleSubCellIsSkipped) {
  StubWorld w(kGrid);
  const TileType t = MakeTile(89, 2, 2,
      {{true, 0}, {false, 0}, {true, 0}, {true, 0}});
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_TRUE(r.ret);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(w.Get(11, 20).tile, 0);   // 空洞那一格没被碰
  EXPECT_EQ(w.Get(11, 21).tile, 89);
}

TEST(PlaceTile, SubCellHeightAddsToBaseLevel) {
  StubWorld w(kGrid);
  const TileType t = MakeTile(89, 2, 2,
      {{true, 0}, {true, 4}, {true, 2}, {true, 0}});
  Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_EQ(w.Get(10, 20).level, 3);
  EXPECT_EQ(w.Get(11, 20).level, 7);
  EXPECT_EQ(w.Get(10, 21).level, 5);
}

TEST(PlaceTile, CellsOutsideDiamondAreSkipped) {
  StubWorld w(kGrid);
  const TileType t =
      MakeTile(89, 3, 3, std::vector<std::pair<bool, int>>(9, {true, 0}));
  const Outcome r = Place(w, kGrid, t, CellCoord{1, 20}, 0, 0, 3, 5, false);
  EXPECT_TRUE(r.ret);
  // 基准里只有这 6 格被写到；(1,21)/(1,22)/(2,22) 落在菱形外
  EXPECT_EQ(w.Get(1, 20).subtile, 0);
  EXPECT_EQ(w.Get(2, 20).subtile, 1);
  EXPECT_EQ(w.Get(3, 22).subtile, 8);
  EXPECT_EQ(w.Get(1, 21).tile, 0);
  EXPECT_EQ(w.Get(2, 22).tile, 0);
}

TEST(PlaceTile, DecidedCellOutsideAllowRangeAborts) {
  StubWorld w(kGrid);
  w.Preset(11, 20, 500, 0, 0);
  const TileType t = MakeTile(89, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_FALSE(r.ret);
  EXPECT_FALSE(r.ok);
  // 中断发生在第二格，第一格已经写下去了——原版不回滚
  EXPECT_EQ(w.Get(10, 20).tile, 89);
  EXPECT_EQ(w.Get(11, 20).tile, 500);
}

// ⚠️ allow 区间**包住**旧砖号时反而也失败（ret=0, ok=0）。
// 那一格的工作归属是 0，而 blob_id=5 ≠ 0 且旧砖已定 → 走 0x57B6B6 的
// `blob_id != -1` 出口，allow 那道闸根本没轮到。基准实测如此。
TEST(PlaceTile, AllowRangeDoesNotHelpWhenBlobMismatch) {
  StubWorld w(kGrid);
  w.Preset(11, 20, 500, 0, 0);
  const TileType t = MakeTile(89, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 400, 600, 3, 5, false);
  EXPECT_FALSE(r.ret);
  EXPECT_FALSE(r.ok);
}

// no_blob_tracking：blob_id 被清零，工作数组不读不写，恒走"同块"的朝向差检查。
TEST(PlaceTile, NoTrackSameFacingStillFailsOnAllowGate) {
  StubWorld w(kGrid);
  w.Preset(10, 20, 89, 0, 0);
  const TileType t = MakeTile(89, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, true);
  EXPECT_FALSE(r.ret);
  EXPECT_TRUE(r.ok);      // ← 静默失败：返回 false 却不动 *ok（0x57B784）
}

TEST(PlaceTile, NoTrackFacingClashSetsNotOk) {
  StubWorld w(kGrid);
  w.Preset(10, 20, 105, 0, 0);   // facing[105-89=16]=0，新砖 facing[0]=4，差 4
  const TileType t = MakeTile(89, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, true);
  EXPECT_FALSE(r.ret);
  EXPECT_FALSE(r.ok);
}

TEST(PlaceTile, NoTrackInsideAllowRangeSucceeds) {
  StubWorld w(kGrid);
  w.Preset(10, 20, 89, 0, 0);
  const TileType t = MakeTile(89, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 89, 130, 3, 5, true);
  EXPECT_TRUE(r.ret);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(w.Get(11, 21).tile, 89);
  EXPECT_EQ(w.Get(11, 21).blob, 0);   // no_track 下工作数组一直没被写
}

// ⚠️ 属于别的块、且旧砖与新砖**同族**时，原版直接判定整块成功返回，
// 后面的子格一格都不贴。看着像漏了 continue，但基准确认就是这样。
TEST(PlaceTile, OtherBlobSameFamilyReturnsEarly) {
  StubWorld w(kGrid);
  w.Preset(11, 20, 89, 0, 0);       // family[0] = 0
  w.PresetBlob(11, 20, 7);
  const TileType t = MakeTile(90, 2, 2, kFlat4);   // family[1] = 0，同族
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_TRUE(r.ret);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(w.Get(10, 20).tile, 90);   // 只有第一格
  EXPECT_EQ(w.Get(10, 21).tile, 0);    // 提前返回，后面没贴
}

TEST(PlaceTile, OtherBlobDifferentFamilyFails) {
  StubWorld w(kGrid);
  w.Preset(11, 20, 89, 0, 0);       // family[0] = 0
  w.PresetBlob(11, 20, 7);
  const TileType t = MakeTile(92, 2, 2, kFlat4);   // family[3] = 1，不同族
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_FALSE(r.ret);
  EXPECT_FALSE(r.ok);
}

// blob_id = -1：遇到已定砖的格子只是**跳过**，不算失败。
TEST(PlaceTile, MinusOneBlobSkipsDecidedCells) {
  StubWorld w(kGrid);
  w.Preset(11, 20, 500, 0, 0);
  const TileType t = MakeTile(89, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, -1, false);
  EXPECT_TRUE(r.ret);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(w.Get(11, 20).tile, 500);   // 跳过
  EXPECT_EQ(w.Get(11, 21).tile, 89);    // 其余照贴
  EXPECT_EQ(w.Get(11, 21).blob, -1);
}

// 同块 + 旧砖已定 + 不在 allow 区间 + 不受保护 → 静默失败（ret=0 但 ok 不变）。
// 调用方 0x57B1E8 只看 *ok，所以这条路对它等于"没失败"。
TEST(PlaceTile, SilentFailureLeavesOkUntouched) {
  StubWorld w(kGrid);
  w.Preset(11, 20, 500, 0, 0);
  w.PresetBlob(11, 20, 5);
  const TileType t = MakeTile(89, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_FALSE(r.ret);
  EXPECT_TRUE(r.ok);
}

// 旧砖是受保护地形（CliffSet 起 0x28 块）且新砖是岸砖 → 0x4863D0 判定，ok 被清
TEST(PlaceTile, ProtectedTerrainCellSetsNotOk) {
  StubWorld w(kGrid);
  w.Preset(11, 20, 40, 0, 0);     // CliffSet = 39，40 在 [39, 39+0x28)
  w.PresetBlob(11, 20, 5);
  const TileType t = MakeTile(89, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_FALSE(r.ret);
  EXPECT_FALSE(r.ok);
}

// 旧砖是岸砖、新砖是受保护地形 → 走另一份判据 0x578D80
TEST(PlaceTile, ProtectedNewTileOverShoreSetsNotOk) {
  StubWorld w(kGrid);
  w.Preset(11, 20, 100, 0, 0);    // 岸砖区间 [89, 89+0x2A)
  w.PresetBlob(11, 20, 5);
  const TileType t = MakeTile(40, 2, 2, kFlat4);   // CliffSet 内
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_FALSE(r.ret);
  EXPECT_FALSE(r.ok);
}

TEST(PlaceTile, ShoreOverShoreIsSilentFailure) {
  StubWorld w(kGrid);
  w.Preset(11, 20, 100, 0, 0);
  w.PresetBlob(11, 20, 5);
  const TileType t = MakeTile(95, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_FALSE(r.ret);
  EXPECT_TRUE(r.ok);
}

// 瀑布组：组内第 1/2 块无条件保护；第 0/3 块要看子格号（东向的例外是 0 与 4）
TEST(PlaceTile, WaterfallMiddleTileIsAlwaysProtected) {
  StubWorld w(kGrid);
  w.Preset(11, 20, 540, 1, 0);    // WaterfallEast = 539，540 是组内第 1 块
  w.PresetBlob(11, 20, 5);
  const TileType t = MakeTile(89, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_FALSE(r.ret);
  EXPECT_FALSE(r.ok);
}

TEST(PlaceTile, WaterfallEdgeTileExemptSubTilePasses) {
  StubWorld w(kGrid);
  w.Preset(11, 20, 539, 0, 0);    // 组内第 0 块，子格 0 是例外 → 不保护
  w.PresetBlob(11, 20, 5);
  const TileType t = MakeTile(89, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_FALSE(r.ret);
  EXPECT_TRUE(r.ok);              // 静默失败，不是保护性失败
}

TEST(PlaceTile, WaterfallEdgeTileOtherSubTileIsProtected) {
  StubWorld w(kGrid);
  w.Preset(11, 20, 539, 2, 0);    // 子格 2 不在例外里 → 保护
  w.PresetBlob(11, 20, 5);
  const TileType t = MakeTile(89, 2, 2, kFlat4);
  const Outcome r = Place(w, kGrid, t, CellCoord{10, 20}, 0, 0, 3, 5, false);
  EXPECT_FALSE(r.ret);
  EXPECT_FALSE(r.ok);
}

TEST(PlaceTile, SingleCellTile) {
  StubWorld w(kGrid);
  const TileType t = MakeTile(89, 1, 1, {{true, 0}});
  const Outcome r = Place(w, kGrid, t, CellCoord{12, 18}, 0, 0, 7, 3, false);
  EXPECT_TRUE(r.ret);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(w.Get(12, 18).tile, 89);
  EXPECT_EQ(w.Get(12, 18).level, 7);
  EXPECT_EQ(w.Get(12, 18).blob, 3);
}

// 静态表本身：两张表都是 42 项，越界索引必须判失败。
TEST(PlaceTile, StaticTablesMatchOriginal) {
  EXPECT_EQ(kShoreFamily[0], 0);
  EXPECT_EQ(kShoreFamily[41], 25);
  EXPECT_EQ(kShoreFacing[0], 4);
  EXPECT_EQ(kShoreFacing[41], 4);
  // 0x2A = 42 与 ShorePieces 的 TilesInSet 相等，见 theater-data.md §四
  EXPECT_TRUE(IsShoreTile(89, 89));
  EXPECT_TRUE(IsShoreTile(89 + 41, 89));
  EXPECT_FALSE(IsShoreTile(89 + 42, 89));
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
