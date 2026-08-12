// CLI 流水线冒烟测试：合成 theater/砖表，跑完整条流水线 + 组装 .map。
//
// 不依赖游戏数据（rulesmd.ini / .mix / .tmp 全不碰），砖表与 theater 都是
// 合成值，只为验证 RunPipeline 全阶段不崩、输出结构完整（[Map] 尺寸 /
// [IsoMapPack5] / [OverlayPack] / [Waypoints] / [Terrain] 各节齐全）。
// 字节级对拍走 e2e（开游戏生成真 RandMap.Map 逐字节 diff）。
#include "cliworld.h"
#include "mapwriter.h"
#include "pipeline.h"
#include "rmg/map_seed_params.h"
#include "rmg/theater.h"
#include "rules_loader.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace ra2 {
namespace cli {
namespace {

// 全字段非负（避免 -1 触发 IsPaveTile 之类"裸区间比较"的边界分支），
// 砖号区间互不重叠、全部落在合成砖表内。
rmg::TheaterData SyntheticTheater() {
  rmg::TheaterData t;
  t.total_tiles = 900;
  t.water_set = 100;
  t.shore_pieces = 300;
  t.green_tile = 500;
  t.clear_to_green_lat = 501;
  t.clear_tile = 502;
  t.rough_tile = 503;
  t.sand_tile = 504;
  t.clear_to_sand_lat = 505;
  t.waterfall_east = 600;
  t.waterfall_west = 601;
  t.waterfall_north = 602;
  t.waterfall_south = 603;
  t.water_cliffs = 604;
  t.ramp_base = 700;
  t.bridge_set = 605;
  t.black_tile = 606;
  t.cliff_set = 607;
  t.cliff_ramps = 608;
  t.ramp_smooth = 609;
  t.water_caves = 610;
  t.wood_bridge_set = 611;
  t.destroyable_cliffs = 612;
  t.paved_roads = 800;
  t.paved_road_ends = 801;
  t.slope_set_pieces = 802;
  t.pave_tile = 803;
  t.misc_pave_tile = 804;
  // SetupLAT（0x47CA80）会读的四个组。必须给合法值：这几个字段默认 -1，
  // 而段 1/段 4 的判据是 `tile ∈ [start, start+0xF]` 不带 -1 保护——合成
  // 数据里 tile 0（未定砖）会掉进 [-1, 14]，整片被误改成粗草过渡片，
  // 阶段 6 判陆地全死、出生点一个都出不来。
  t.clear_to_rough_lat = 506;
  t.clear_to_pave_lat = 805;
  t.medians = 806;
  t.water_bridge = 807;
  return t;
}

// 900 块 1×1 全实心砖：subcell 高度 0、terrain_type 0（ground 0）。
std::vector<rmg::TileType> SyntheticTiles() {
  std::vector<rmg::TileType> tiles(900);
  for (int i = 0; i < 900; ++i) {
    rmg::TileType& t = tiles[static_cast<std::size_t>(i)];
    t.tile_number = i;
    t.width = 1;
    t.height = 1;
    t.cells.push_back(rmg::TileSubCell{true, 0, 0, 0});
  }
  return tiles;
}

// test_smoke.sed 实机采样参数（Seed=60556），测试内固定。
rmg::MapSeedParams SmokeParams() {
  rmg::MapSeedParams p;
  p.theater = 1;
  p.map_type = 1;
  p.resources = 3;
  p.ruggedness = 64;
  p.time = 3;
  p.water_amount = 0;
  p.num_players = 2;
  p.tiberium = 60;
  p.tiberium_layout = 29;
  p.vegetation = 78;
  p.urban_presence = 12;
  p.width = 0;
  p.height = 0;
  p.accessibility = 40;
  p.region_size = 62;
  p.seed = 60556;
  return p;
}

RmgRules DefaultRules() {
  RmgRules r;
  r.neutral_tech_buildings = {"CAAIRP", "CATHOSP", "CAOILD",
                              "CAOUTP", "CAMACH", "CAPOWR"};
  r.ground_foot.assign(12, 1.0f);
  return r;
}

void ExpectMapSections(const std::string& map) {
  // 各节存在（[Waypoints]/[Terrain] 取决于这局有没有点/地形，
  // [Structures] 可能没有——这局不一定生成建筑）。
  EXPECT_NE(std::string::npos, map.find("[Map]\n"));
  EXPECT_NE(std::string::npos, map.find("[IsoMapPack5]\n"));
  EXPECT_NE(std::string::npos, map.find("[OverlayPack]\n"));
  // [Map] 的尺寸与层级
  EXPECT_NE(std::string::npos, map.find("Size=0,0,74,82\n"));
  EXPECT_NE(std::string::npos, map.find("LocalSize=2,5,70,70\n"));
  EXPECT_NE(std::string::npos, map.find("Level=4\n"));
}

// 冒烟：smoke 参数（MapType=1，无河湖支路）+ 合成数据，全流水线跑通。
TEST(CliPipeline, Smoke70x70) {
  const rmg::MapSeedParams params = SmokeParams();
  const rmg::TheaterData theater = SyntheticTheater();
  const std::vector<rmg::TileType> tiles = SyntheticTiles();
  const RmgRules rules = DefaultRules();

  PipelineResult result;
  std::string err;
  ASSERT_TRUE(RunPipeline(params, 60556u, theater, tiles, rules,
                          static_cast<int>(tiles.size()), &result, &err))
      << err;

  EXPECT_EQ(70, result.map_w);
  EXPECT_EQ(70, result.map_h);
  EXPECT_GE(result.waypoints.size(), 2u);   // 2 玩家 → 至少 1..2 号点
  EXPECT_GT(result.terrains.size(), 0u);    // 有地形记录

  const std::string map = BuildMapFile(result, params.theater, 4);
  ExpectMapSections(map);
  // smoke 的陆地足够，出生点与地形记录都在
  EXPECT_NE(std::string::npos, map.find("[Waypoints]\n"));
  EXPECT_NE(std::string::npos, map.find("[Terrain]\n"));
  EXPECT_NE(std::string::npos, map.find("Theater=SNOW\n"));
}

// MapType=3 河湖支路：WaterAmount>20 触发 AddRiversAndLakes + 阶段 4 造坡。
// 合成砖表下陆地可能不足，出生点/地形记录可能为空——只验证不崩 + 主节齐全。
TEST(CliPipeline, MapType3RiversAndRamps) {
  rmg::MapSeedParams params = SmokeParams();
  params.map_type = 3;
  params.water_amount = 60;

  const rmg::TheaterData theater = SyntheticTheater();
  const std::vector<rmg::TileType> tiles = SyntheticTiles();
  const RmgRules rules = DefaultRules();

  PipelineResult result;
  std::string err;
  ASSERT_TRUE(RunPipeline(params, 60556u, theater, tiles, rules,
                          static_cast<int>(tiles.size()), &result, &err))
      << err;
  EXPECT_EQ(70, result.map_w);

  const std::string map = BuildMapFile(result, params.theater, 4);
  ExpectMapSections(map);
}

// 温和气候（Theater=0）：阶段 10 的 LAT 表按温和 theater 走一遍。
TEST(CliPipeline, TemperateTheater) {
  rmg::MapSeedParams params = SmokeParams();
  params.theater = 0;
  params.seed = 12345;

  const rmg::TheaterData theater = SyntheticTheater();
  const std::vector<rmg::TileType> tiles = SyntheticTiles();
  const RmgRules rules = DefaultRules();

  PipelineResult result;
  std::string err;
  ASSERT_TRUE(RunPipeline(params, 12345u, theater, tiles, rules,
                          static_cast<int>(tiles.size()), &result, &err))
      << err;

  const std::string map = BuildMapFile(result, params.theater, 4);
  EXPECT_NE(std::string::npos, map.find("Theater=TEMPERATE\n"));
  ExpectMapSections(map);
}

}  // namespace
}  // namespace cli
}  // namespace ra2
