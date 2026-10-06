#include "start_points.h"

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"

namespace ra2 {
namespace rmg {
namespace {

class InfluenceWorld : public StartPointWorld {
 public:
  explicit InfluenceWorld(const MapGrid& grid)
      : grid_(grid), cells_(static_cast<std::size_t>(grid.side()) * grid.side()) {}

  int CellZoneId(const CellCoord&) const override { return 0; }
  int MovementZoneType(const CellCoord&, int) const override { return 0; }
  int IsoTile(const CellCoord& c) const override { return At(c).tile; }
  int Level(const CellCoord&) const override { return 0; }
  int Ramp(const CellCoord&) const override { return 0; }
  CellCoord ReadBackCoord(const CellCoord& c) override { return c; }
  CellCoord Waypoint(int index) override { return waypoints_[static_cast<std::size_t>(index)]; }
  void SetWaypoint(int index, const CellCoord& c) override {
    waypoints_[static_cast<std::size_t>(index)] = c;
  }
  void ResetStartLocationTable() override {}
  void SetStartLocation(int, const CellCoord&) override {}
  void PlaceRandomBuilding(RegionClass*) override {}

  void SetTile(const CellCoord& c, int tile) { At(c).tile = tile; }

 private:
  struct Cell {
    int tile = 0;
  };

  Cell& At(const CellCoord& c) { return cells_[static_cast<std::size_t>(grid_.WorkIndex(c))]; }
  const Cell& At(const CellCoord& c) const {
    return cells_[static_cast<std::size_t>(grid_.WorkIndex(c))];
  }

  MapGrid grid_;
  std::vector<Cell> cells_;
  std::vector<CellCoord> waypoints_ = std::vector<CellCoord>(8);
};

TEST(StartInfluence, FloodsUndecidedTilesAndResetsQueuedPerWaypoint) {
  const MapGrid grid(8, 8);
  std::vector<PaintWorkCell> work(
      static_cast<std::size_t>(grid.side()) * static_cast<std::size_t>(grid.side()));
  grid.ForEachCell([&](const CellCoord& c) {
    work[static_cast<std::size_t>(grid.WorkIndex(c))].queued = -1;
  });

  InfluenceWorld world(grid);
  const CellCoord first{8, 8};
  const CellCoord blocked{8, 9};
  const CellCoord second{7, 10};
  world.SetWaypoint(0, first);
  world.SetWaypoint(1, second);
  world.SetTile(blocked, 89);

  StartPointContext ctx;
  ctx.grid = grid;
  ctx.side = grid.side();
  ctx.work = &work;
  ctx.world = &world;
  ctx.num_players = 2;

  EXPECT_TRUE(MarkStartInfluence(ctx));

  EXPECT_TRUE(work[static_cast<std::size_t>(grid.WorkIndex(first))].flag45);
  EXPECT_FALSE(work[static_cast<std::size_t>(grid.WorkIndex(blocked))].flag45);
  EXPECT_TRUE(work[static_cast<std::size_t>(grid.WorkIndex(second))].flag45);
  EXPECT_EQ(2, work[static_cast<std::size_t>(grid.WorkIndex(second))].queued);
  EXPECT_NE(-1, work[static_cast<std::size_t>(grid.WorkIndex(first))].queued);
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
