// 阶段 4 系列测试共享的 MakingWorld 桩实现。
//
// 语义与 dump_making_regions_golden.py / dump_ramps_golden.py 的 oracle 完全
// 对齐：kCellSlots = 0x40000（512×512，与原版 GetCellAt 一致）、
// SlotOf = y×512+x、exists_ 由 MapGrid.ForEachCell（菱形）初始化、
// 界外格读写落到共享哑元 dummy_（原版 0xABDC50 语义）。
#pragma once

#include "making_regions.h"

#include <cstdint>
#include <vector>

namespace ra2 {
namespace rmg {

// 与 dump 脚本的合成砖规格逐项一致：
//   CliffSet 39..78 2×2 平砖（全 present、高 0）
//   SlopeSetPieces 200..209 / PavedRoads 300..320 / PavedRoadEnds 340..343 1×1
constexpr int kCliffSet = 39;
constexpr int kSSP = 200;
constexpr int kPaved = 300;
constexpr int kPavedEnd = 340;

inline TheaterData Temperate() {
  TheaterData t;
  t.water_set = 314;
  t.shore_pieces = 89;
  t.green_tile = 493;
  t.clear_to_green_lat = 494;
  t.waterfall_east = 539;
  t.waterfall_west = 547;
  t.waterfall_north = 543;
  t.waterfall_south = 414;
  t.water_cliffs = 148;
  t.ramp_base = 29;
  t.bridge_set = 277;
  t.cliff_set = kCliffSet;
  t.cliff_ramps = 601;
  t.ramp_smooth = 621;
  t.water_caves = -1;
  t.wood_bridge_set = -1;
  t.destroyable_cliffs = 761;
  t.paved_roads = kPaved;          // 0xABBEC8
  t.paved_road_ends = kPavedEnd;   // 0xABBEC4
  t.slope_set_pieces = kSSP;       // 0xABC1F8
  return t;
}

constexpr int kCellSlots = 0x40000;   // 引擎侧格子数组 512×512 定长

inline int SlotOf(const CellCoord& c) {
  const int idx = (c.y << 9) + c.x;
  return (idx < 0 || idx >= kCellSlots) ? -1 : idx;
}

struct Cell {
  int tile = 0;          // +0x38
  int subtile = 0;       // +0x11A
  std::int8_t level = 0; // +0x11B
  int ramp = 0;          // +0x11C
  int overlay = 0;       // +0x44
  int overlay_data = 0;  // +0x11E
};

class MakingStub : public MakingWorld {
 public:
  MakingStub(const MapGrid& g, std::vector<PaintWorkCell>* work)
      : grid_(g), work_(work), cells_(kCellSlots), exists_(kCellSlots, 0) {
    g.ForEachCell([&](const CellCoord& c) { exists_[SlotOf(c)] = 1; });
  }

  // ── RegionWorld ∩ TileWorld ──
  bool CellExists(const CellCoord& c) const override {
    const int s = SlotOf(c);
    return s >= 0 && exists_[s] != 0;
  }
  void SetDummyCoord(const CellCoord& c) override { dummy_coord_ = c; }
  int IsoTile(const CellCoord& c) const override { return At(c).tile; }
  void SetIsoTile(const CellCoord& c, int v) override { Mut(c).tile = v; }
  void SetSubTile(const CellCoord& c, int v) override { Mut(c).subtile = v; }
  void SetLevel(const CellCoord& c, int v) override {
    Mut(c).level = static_cast<std::int8_t>(v);
  }

  // ── RegionWorld ──
  int Level(const CellCoord& c) const override { return At(c).level; }

  // ── TileWorld ──
  int SubTile(const CellCoord& c) const override { return At(c).subtile; }
  void SetRamp(const CellCoord& c, int v) override { Mut(c).ramp = v; }
  int DummyIsoTile() const override { return dummy_.tile; }
  int DummySubTile() const override { return dummy_.subtile; }
  void SetDummyTile(int tile, int subtile, int level) override {
    dummy_.tile = tile;
    dummy_.subtile = subtile;
    dummy_.level = static_cast<std::int8_t>(level);
  }
  void SetDummyIsoTile(int tile) override { dummy_.tile = tile; }
  int WorkBlob(const CellCoord& c) const override {
    return (*work_)[WorkIdx(c)].blob_id;
  }
  void SetWorkBlob(const CellCoord& c, int v) override {
    (*work_)[WorkIdx(c)].blob_id = v;
  }

  // ── MakingWorld ──
  int Ramp(const CellCoord& c) const override { return At(c).ramp; }
  int Overlay(const CellCoord& c) const override { return At(c).overlay; }
  void SetOverlay(const CellCoord& c, int v) override { Mut(c).overlay = v; }
  int OverlayData(const CellCoord& c) const override {
    return At(c).overlay_data;
  }
  void SetOverlayData(const CellCoord& c, int v) override {
    Mut(c).overlay_data = v;
  }
  const TileType* TileByIndex(int index) const override {
    for (const TileType& t : tiles_) {
      if (t.tile_number == index) return &t;
    }
    return nullptr;
  }
  void PlaceRepairHut(const CellCoord&) override { ++huts_; }

  // ── 测试辅助 ──
  void Seed(const CellCoord& c, int tile, int subtile, int level, int ramp,
            int overlay, int overlay_data) {
    Cell& x = Mut(c);
    x.tile = tile;
    x.subtile = subtile;
    x.level = static_cast<std::int8_t>(level);
    x.ramp = ramp;
    x.overlay = overlay;
    x.overlay_data = overlay_data;
  }
  Cell Get(const CellCoord& c) const { return At(c); }
  CellCoord dummy_coord() const { return dummy_coord_; }
  int huts() const { return huts_; }

 private:
  std::size_t WorkIdx(const CellCoord& c) const {
    long long i = static_cast<long long>(c.y) * grid_.side() + c.x;
    if (i < 0) i = 0;
    if (i >= static_cast<long long>(work_->size())) i = work_->size() - 1;
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
  std::vector<TileType> tiles_ = [] {
    std::vector<TileType> ts;
    auto add = [&](int base, int n, int w, int h) {
      for (int k = 0; k < n; ++k) {
        TileType t;
        t.tile_number = base + k;
        t.width = w;
        t.height = h;
        t.cells.assign(static_cast<std::size_t>(w * h),
                       TileSubCell{true, 0, 0});
        ts.push_back(t);
      }
    };
    add(kCliffSet, 40, 2, 2);
    add(kSSP, 10, 1, 1);
    add(kPaved, 21, 1, 1);
    add(kPavedEnd, 4, 1, 1);
    return ts;
  }();
  Cell dummy_;
  CellCoord dummy_coord_;
  int huts_ = 0;
};

}  // namespace rmg
}  // namespace ra2
