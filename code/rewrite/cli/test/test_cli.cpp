// CLI 流水线冒烟测试：合成 theater/砖表，跑完整条流水线 + 组装 .map。
//
// 不依赖游戏数据（rulesmd.ini / .mix / .tmp 全不碰），砖表与 theater 都是
// 合成值，只为验证 RunPipeline 全阶段不崩、输出结构完整（[Map] 尺寸 /
// [IsoMapPack5] / [OverlayPack] / [OverlayDataPack] / [Waypoints] / [Terrain]
// 各节齐全）。字节级对拍走 e2e（开游戏生成真 RandMap.Map 逐字节 diff）。
#include "cliworld.h"
#include "mapwriter.h"
#include "pipeline.h"
#include "rmg/map_seed_params.h"
#include "rmg/theater.h"
#include "rules_loader.h"

#include <algorithm>
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

TEST(RulesLoader, ReadsNeutralTechBuildingsFromAiSection) {
  RmgRules rules;
  ASSERT_TRUE(LoadRmgRules(
      "[General]\nMaxTrees=321\n[AI]\n"
      "NeutralTechBuildings=CAAIRP, CATHOSP,CAOUTP\n",
      &rules));

  ASSERT_EQ(3u, rules.neutral_tech_buildings.size());
  EXPECT_EQ("CAAIRP", rules.neutral_tech_buildings[0]);
  EXPECT_EQ("CATHOSP", rules.neutral_tech_buildings[1]);
  EXPECT_EQ("CAOUTP", rules.neutral_tech_buildings[2]);
  EXPECT_EQ(321, rules.max_trees);
}

TEST(RulesLoader, ReadsRmgmdOverlayValues) {
  RmgRules rules;
  ASSERT_TRUE(LoadRmgRules(
      "[General]\nRMGMinimumTiberium=900\n"
      "RMGMaximumTiberium=1050\nMaxTrees=600\n"
      "RMGVegetationMinimums=60,60,60,60,60\n"
      "RMGVegetationMaximums=100,100,100,100,100\n",
      &rules));

  EXPECT_EQ(900, rules.min_tiberium);
  EXPECT_EQ(1050, rules.max_tiberium);
  EXPECT_EQ(600, rules.max_trees);
}

TEST(MapWriter, EncodesIsoCoordinatesAsGameWaypointInteger) {
  PipelineResult r;
  r.map_w = 70;
  r.map_h = 70;
  r.cells.resize(0x40000);
  r.exists.resize(0x40000);
  r.waypoints.resize(2);
  r.waypoints[0] = rmg::CellCoord{90, 22};
  r.waypoints[1] = rmg::CellCoord{40, 105};
  r.start_locations.push_back(StartLocationRecord{0, 49, 110});
  r.start_locations.push_back(StartLocationRecord{1, 78, 27});
  r.terrains.push_back(TerrainRecord{"TREE01", 49, 110});
  r.terrains.push_back(TerrainRecord{"TREE02", 93, 179});

  const std::string map = BuildMapFile(r, 0, 4);
  EXPECT_NE(std::string::npos, map.find("[Basic]\n"));
  EXPECT_NE(std::string::npos, map.find("MultiplayerOnly=1\n"));
  EXPECT_NE(std::string::npos, map.find("[Waypoints]\n"));
  // 键 = y*1000+x（y 在千位）：游戏读路径 0x68BE0C 实锤（V%1000→X、V/1000→Y）
  EXPECT_NE(std::string::npos, map.find("0=22090\n"));
  EXPECT_NE(std::string::npos, map.find("1=105040\n"));
  EXPECT_NE(std::string::npos, map.find("[Terrain]\n"));
  EXPECT_NE(std::string::npos, map.find("110049=TREE01\n"));
  EXPECT_NE(std::string::npos, map.find("179093=TREE02\n"));
}

void ExpectMapSections(const std::string& map) {
  // 各节存在（[Waypoints]/[Terrain] 取决于这局有没有点/地形，
  // [Structures] 可能没有——这局不一定生成建筑）。
  EXPECT_NE(std::string::npos, map.find("[Map]\n"));
  EXPECT_NE(std::string::npos, map.find("[IsoMapPack5]\n"));
  EXPECT_NE(std::string::npos, map.find("[OverlayPack]\n"));
  EXPECT_NE(std::string::npos, map.find("[OverlayDataPack]\n"));
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

TEST(CliPipeline, StartPointsUseInsetLocalRect) {
  rmg::MapSeedParams params;
  params.theater = 0;
  params.map_type = 3;
  params.resources = 3;
  params.ruggedness = 31;
  params.time = 0;
  params.water_amount = 30;
  params.num_players = 2;
  params.tiberium = 60;
  params.tiberium_layout = 52;
  params.vegetation = 83;
  params.urban_presence = 65;
  params.width = 0;
  params.height = 0;
  params.accessibility = 39;
  params.region_size = 68;
  params.seed = 62298;

  const rmg::TheaterData theater = SyntheticTheater();
  const std::vector<rmg::TileType> tiles = SyntheticTiles();
  const RmgRules rules = DefaultRules();

  PipelineResult result;
  std::string err;
  ASSERT_TRUE(RunPipeline(params, 62298u, theater, tiles, rules,
                          static_cast<int>(tiles.size()), &result, &err))
      << err;

  const auto it = std::find_if(result.rng_checkpoints.begin(),
                               result.rng_checkpoints.end(),
                               [](const RngCheckpoint& cp) {
                                 return cp.name == "after_start_points";
                               });
  // SplitRegion 防重（0x58D9EA/0x58DA5B）已在实机取证闭环：
  //   实机 = 防重有效（Set_Dimensions 0x5671A5 按 side 初始化 0x89A304 表）
  //   2026-09-28 实机帧 08→11 四帧 0 diff、RNG 检查点逐位吻合。
  // 期望值 = 防重版 CLI 的**合成砖表**结果（与 LiveRun 的 61638/tries=44 不同，
  // 合成 profile 下区域划分不同）；改动 SplitRegion 后此期望值需同步刷新。
  // 数值取自 0x594876 内缩修复（usable 传未缩矩形、AreaIsPassable 走
  // +0xF4/+0xF8 版菱形判定）落地后的实际行为；该行为 CLI 全程与 live
  // rmg_done 保持菱形内全字段 0 差异（2026-10-06 验证）。
  ASSERT_NE(result.rng_checkpoints.end(), it);
  EXPECT_EQ(61928, it->calls);
  ASSERT_GE(result.start_trace.size(), 2u);
  EXPECT_EQ("pick", result.start_trace[1].kind);
  EXPECT_EQ(76, result.start_trace[1].tries);
  const auto first_cand = std::find_if(
      result.start_trace.begin(), result.start_trace.end(),
      [](const rmg::StartTraceEntry& tr) { return tr.kind == "cand"; });
  ASSERT_NE(result.start_trace.end(), first_cand);
  EXPECT_EQ(0, result.start_trace[1].region);
  EXPECT_EQ(2, result.start_trace[1].start_points);
}

TEST(CliWorldTechBuildings, UnlimboFailsWithoutObjectRegistration) {
  rmg::MapGrid grid(12, 12);
  const rmg::TheaterData theater = SyntheticTheater();
  const std::vector<rmg::TileType> tiles = SyntheticTiles();
  rmg::Randomizer rng(1);
  std::vector<rmg::PaintWorkCell> work(
      static_cast<std::size_t>(grid.side()) * static_cast<std::size_t>(grid.side()));
  CliWorld world(grid, theater, tiles, rng, work,
                 static_cast<int>(tiles.size()));
  world.InitCells(true);
  world.set_neutral_tech_buildings({"CAMACH"});
  world.set_foundations({{"CAMACH", {{0, 0}, {1, 0}, {0x7FFF, 0x7FFF}}}});

  const int handle = world.CreateBuilding(0);
  ASSERT_GE(handle, 0);
  ASSERT_EQ(0u, world.buildings().size());
  EXPECT_FALSE(world.Unlimbo(handle, rmg::CellCoord{8, 5}));
  EXPECT_TRUE(world.buildings().empty());
  EXPECT_EQ(0, world.Field0xE4(rmg::CellCoord{8, 5}));
  EXPECT_EQ(0, world.Field0xE4(rmg::CellCoord{9, 5}));
}

TEST(CliWorldTechBuildings, UnlimboCanWriteWhenObjectRegistrationExists) {
  rmg::MapGrid grid(12, 12);
  const rmg::TheaterData theater = SyntheticTheater();
  const std::vector<rmg::TileType> tiles = SyntheticTiles();
  rmg::Randomizer rng(1);
  std::vector<rmg::PaintWorkCell> work(
      static_cast<std::size_t>(grid.side()) * static_cast<std::size_t>(grid.side()));
  CliWorld world(grid, theater, tiles, rng, work,
                 static_cast<int>(tiles.size()));
  world.InitCells(true);
  world.set_neutral_tech_buildings({"CAMACH"});
  world.set_foundations({{"CAMACH", {{0, 0}, {1, 0}, {0x7FFF, 0x7FFF}}}});
  world.set_tech_object_registration(true);

  const int handle = world.CreateBuilding(0);
  (void)world.Foundation(handle);
  EXPECT_TRUE(world.Unlimbo(handle, rmg::CellCoord{8, 5}));
  ASSERT_EQ(1u, world.buildings().size());
  EXPECT_EQ("CAMACH", world.buildings()[0].type);
  EXPECT_EQ(8, world.buildings()[0].x);
  EXPECT_EQ(5, world.buildings()[0].y);
  EXPECT_NE(0, world.Field0xE4(rmg::CellCoord{8, 5}));
  EXPECT_NE(0, world.Field0xE4(rmg::CellCoord{9, 5}));
}

TEST(CliPipeline, WaterAmountTwentyRunsLakeOnlyDispatch) {
  rmg::MapSeedParams params;
  params.theater = 0;
  params.map_type = 3;
  params.resources = 3;
  params.ruggedness = 31;
  params.time = 0;
  params.water_amount = 20;
  params.num_players = 2;
  params.tiberium = 0;
  params.tiberium_layout = 52;
  params.vegetation = 83;
  params.urban_presence = 65;
  params.width = 0;
  params.height = 0;
  params.accessibility = 39;
  params.region_size = 68;

  const rmg::TheaterData theater = SyntheticTheater();
  const std::vector<rmg::TileType> tiles = SyntheticTiles();
  const RmgRules rules = DefaultRules();

  PipelineResult result;
  std::string err;
  ASSERT_TRUE(RunPipeline(params, 62298u, theater, tiles, rules,
                          static_cast<int>(tiles.size()), &result, &err))
      << err;

  const auto find_checkpoint = [&](const char* name) {
    return std::find_if(result.rng_checkpoints.begin(),
                        result.rng_checkpoints.end(),
                        [name](const RngCheckpoint& cp) {
                          return cp.name == name;
                        });
  };
  const auto init = find_checkpoint("after_init_random");
  const auto water = find_checkpoint("after_seeding_water");
  const auto refine = find_checkpoint("after_refine_water");
  ASSERT_NE(result.rng_checkpoints.end(), init);
  ASSERT_NE(result.rng_checkpoints.end(), water);
  ASSERT_NE(result.rng_checkpoints.end(), refine);
  EXPECT_EQ(init->calls, water->calls);
  EXPECT_EQ(water->calls, refine->calls);
}

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
