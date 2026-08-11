// 随机地图流水线：.sed 参数 → 全部地形阶段 → 输出记录（.map 组装用）。
//
// 阶段顺序照抄原版 0x598960（docs/random-map/README.md §流水线）：
//   ComputeMapSize → InitCells（阶段 1）→ SeedWater（阶段 2）→
//   [MapType∈{3,4} 且 WaterAmount>20] AddRiversAndLakes（阶段 2.5）→
//   RefineWaterTiles → InitRegions（阶段 3）→ [MapType∈{3,4}] MakeRegions
//   （阶段 4）→ SpreadGreen + Recalc（阶段 5）→ ComputeStartZoneSelection +
//   PlaceStartPoints（阶段 6）→ [MapType≠0] AddTechBuildings（阶段 7）→
//   PlaceTiberium（阶段 8）→ CreateHills（阶段 9）→ CreateLats（阶段 10）。
//
// 整条流水线包在一个 FpuChopScope 里（原版全局 RC=chop）。
// rng(seed) / NormalDeviate / work(side²) 跨阶段共享，一个都不重建。
#pragma once

#include <string>
#include <vector>

#include "cliworld.h"
#include "rmg/map_grid.h"
#include "rmg/map_seed_params.h"
#include "rmg/randomizer.h"
#include "rules_loader.h"

namespace ra2 {
namespace cli {

// 流水线的产出。格子终态在 cells/exists 里（IsoMapPack5 / OverlayPack 组装用），
// 记录（waypoints/terrain/structures/出生点）单列。
struct PipelineResult {
  int map_w = 0;    // MapSeedClass +0x180（真实宽 W）
  int map_h = 0;    // MapSeedClass +0x184（真实高 H）
  // 全图格子终态：下标 = (y<<9)+x，512×512；exists 标记菱形内。
  std::vector<CliCell> cells;
  std::vector<char> exists;
  std::vector<ra2::rmg::CellCoord> waypoints;   // 1..num_players 是出生点
  std::vector<TerrainRecord> terrains;
  std::vector<BuildingRecord> buildings;
  std::vector<StartLocationRecord> start_locations;
};

// 跑完整条流水线。
//
// seed：随机种子。原版 MapSeedClass +0x74 = -1 时由生成器从系统时钟抽，
// CLI 里 -1 表示"调用方决定"——这里直接用它播种（R250 对种子无限制）。
bool RunPipeline(const ra2::rmg::MapSeedParams& params, unsigned seed,
                 const ra2::rmg::TheaterData& theater,
                 const std::vector<ra2::rmg::TileType>& tiles,
                 const RmgRules& rules, int tile_count, PipelineResult* out,
                 std::string* err);

}  // namespace cli
}  // namespace ra2
