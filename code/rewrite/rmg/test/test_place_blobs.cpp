// 三种 MapType 投放策略（原版 0x59AD10 / 0x59AFA0 / 0x59B200）的黄金对拍
//
// 基准由 code/randmap/dump_place_blobs_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/place_blobs_golden.txt。比的是**整张地图的陆地生长
// 顺序**——跨所有涂块调用，一步错位就炸。
//
// 桩的口径与基准严格一致：IsPaintable 恒真、岸线平滑恒真、
// 全图边界给 (-1,-1,-1,-1)（让 InRegion / DirectionalCost 都走真实分支）。
#include "place_blobs.h"

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

struct PlaceCase {
  int w, h;
  unsigned seed;
  int map_type;
  int players;
  int water;
  Rect usable;
  int expect_cells;
  int expect_rng;
  int expect_stamp;
  const char* order;
};

#include "place_blobs_golden.inc"

class StubWorld : public PaintWorld {
 public:
  bool IsPaintable(const CellCoord&) const override { return true; }
  int IsoTile(const CellCoord& c) const override {
    auto it = tiles_.find(Key(c));
    return it == tiles_.end() ? kWater : it->second;
  }
  void SetIsoTile(const CellCoord& c, int t) override {
    tiles_[Key(c)] = t;
    if (t == 0) order.push_back(c);
  }
  void SetCellSubTile(const CellCoord&, int) override {}
  void SetCellLevel(const CellCoord&, int) override {}
  bool SmoothShoreline(int) override { return true; }
  int ShorePiecesBase() const override { return 900; }
  int WaterTile() const override { return kWater; }

  std::vector<CellCoord> order;

 private:
  static constexpr int kWater = 1;
  static int Key(const CellCoord& c) { return (c.y << 16) | (c.x & 0xFFFF); }
  mutable std::map<int, int> tiles_;
};

std::string Format(const std::vector<CellCoord>& v) {
  std::ostringstream os;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) os << ' ';
    os << v[i].x << ':' << v[i].y;
  }
  return os.str();
}

struct PlaceRun {
  std::vector<CellCoord> order;
  long long rng_calls = 0;
  int stamp = 0;
};

PlaceRun RunCase(const PlaceCase& c) {
  FpuChopScope chop;

  MapGrid grid(c.w, c.h);
  const int side = grid.side();
  std::vector<PaintWorkCell> work(static_cast<size_t>(side) * side);
  StubWorld world;
  Randomizer rng(c.seed);
  NormalDeviate normal(rng);
  int stamp = 0;

  PaintContext ctx;
  ctx.grid = grid;
  ctx.full_bounds = Rect{-1, -1, -1, -1};
  ctx.work = &work;
  ctx.world = &world;
  ctx.rng = &rng;
  ctx.normal = &normal;
  ctx.blob_stamp = &stamp;
  ctx.cell_level = 4;

  PlaceParams p;
  p.num_players = c.players;
  p.water_amount = c.water;
  p.usable = c.usable;

  PlaceBlobs(ctx, p, c.map_type);

  PlaceRun r;
  r.order = world.order;
  r.rng_calls = rng.call_count();
  r.stamp = stamp;
  return r;
}

// 主判据：整张地图的陆地生长顺序逐格一致。
TEST(PlaceBlobs, GrowthOrderMatchesOriginal) {
  for (const PlaceCase& c : kCases) {
    SCOPED_TRACE(testing::Message()
                 << "mt=" << c.map_type << " seed=0x" << std::hex << c.seed);
    const PlaceRun r = RunCase(c);
    EXPECT_EQ(static_cast<int>(r.order.size()), c.expect_cells);
    EXPECT_EQ(Format(r.order), std::string(c.order));
  }
}

// 随机数消耗量：顺序对而次数不对，说明有一路分支白抽了随机数。
TEST(PlaceBlobs, RandomDrawCountMatchesOriginal) {
  for (const PlaceCase& c : kCases) {
    SCOPED_TRACE(testing::Message()
                 << "mt=" << c.map_type << " seed=0x" << std::hex << c.seed);
    EXPECT_EQ(RunCase(c).rng_calls, c.expect_rng);
  }
}

// 块编号：MapType 0 在涂块失败时也自增，所以它是「跳号」行为的独立探针。
TEST(PlaceBlobs, BlobStampMatchesOriginal) {
  for (const PlaceCase& c : kCases) {
    SCOPED_TRACE(testing::Message()
                 << "mt=" << c.map_type << " seed=0x" << std::hex << c.seed);
    EXPECT_EQ(RunCase(c).stamp, c.expect_stamp);
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
