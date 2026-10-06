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

#include <array>
#include <cstdint>
#include <utility>
#include <string>
#include <vector>

#include "cliworld.h"
#include "rmg/map_grid.h"
#include "rmg/map_seed_params.h"
#include "rmg/ore_field.h"
#include "rmg/randomizer.h"
#include "rules_loader.h"

namespace ra2 {
namespace cli {

// 流水线的产出。格子终态在 cells/exists 里（IsoMapPack5 / OverlayPack 组装用），
// 记录（waypoints/terrain/structures/出生点）单列。
struct RngCheckpoint {
  std::string name;
  long long calls = 0;
  int index1 = 0;
  int index2 = 0;
  bool normal_cached = false;
  double normal_value = 0.0;
  bool has_state = false;
  std::array<std::uint32_t, ra2::rmg::Randomizer::kStateSize> state{};
};

struct StageCellSnapshot {
  std::string name;
  std::vector<CliCell> cells;
  // 只有点名要 work 的阶段才会填（每份 21MB，不能全阶段都存）。
  bool has_work = false;
  std::vector<ra2::rmg::PaintWorkCell> work;
};

struct ArlStateSnapshot {
  bool valid = false;
  int side = 0;
  int map_w = 0;
  int map_h = 0;
  int region_size = 0;
  int map_type = 0;
  int accessibility = 0;
  int theater = 0;
  const ra2::rmg::TheaterData* theater_data = nullptr;
  long long rng_calls = 0;
  int rng_index1 = 0;
  int rng_index2 = 0;
  bool normal_cached = false;
  double normal_value = 0.0;
  std::array<std::uint32_t, ra2::rmg::Randomizer::kStateSize> rng_state{};
  std::vector<ra2::rmg::PaintWorkCell> work;
  std::vector<CliCell> cells;
};

struct PipelineResult {
  int map_w = 0;    // MapSeedClass +0x180（真实宽 W）
  int map_h = 0;    // MapSeedClass +0x184（真实高 H）
  // 阶段 5 Recalc 后、0x56C510 Rebuild 前的 zone 表输入（side² × 4B）。
  // 与权威 beforerebuild/zone_table_4b.bin 对拍用；zone 列在喂入时恒为 0
  // （Rebuild 才填）。capture_stage_cells 打开时才拷贝，避免默认开销。
  int zone_table_side = 0;
  std::vector<ra2::rmg::ZoneCell> zone_table_beforerebuild;
  // 全图格子终态：下标 = (y<<9)+x，512×512；exists 标记菱形内。
  std::vector<CliCell> cells;
  std::vector<char> exists;
  std::vector<ra2::rmg::CellCoord> waypoints;   // 1..num_players 是出生点
  std::vector<TerrainRecord> terrains;
  std::vector<BuildingRecord> buildings;
  std::vector<StartLocationRecord> start_locations;
  std::vector<ra2::rmg::TechTraceEntry> tech_trace;
  std::vector<ra2::rmg::StartTraceEntry> start_trace;
  bool capture_stage_cells = false;
  bool capture_stage_work = false;
  bool capture_stage_ground = false;
  bool capture_stage_rng_state = false;   // after_hills 时导完整 RNG 状态（emu 灌入用）
  std::vector<std::string> stage_work_names;   // 空 = 全部阶段都导
  bool capture_arl_state = false;
  long long capture_rng_call_cells = -1;
  std::vector<RngCheckpoint> rng_checkpoints;
  std::vector<StageCellSnapshot> cell_snapshots;
  bool rng_call_cells_valid = false;
  long long rng_call_cells_call = 0;
  std::vector<CliCell> rng_call_cells;
  long long capture_before_rng_call_cells = -1;
  bool before_rng_call_cells_valid = false;
  long long before_rng_call_cells_call = 0;
  std::vector<CliCell> before_rng_call_cells;
  ArlStateSnapshot arl_state;
  std::vector<ra2::rmg::OreTraceEntry> ore_trace;
  std::vector<ra2::rmg::OrePopEntry> ore_pop_trace;
  std::vector<ra2::rmg::OrePopEntry> ore_enq_trace;   // 入队 trace（--dump-ore-enq）
  // 非空时，阶段 8 每片矿田后把 work.queued 写到 「前缀_序号.bin」（emu 逐片对拍用）
  std::string ore_work_dump_prefix;
  long long rng_calls = 0;
  int rng_index1 = 0;
  int rng_index2 = 0;
  std::array<std::uint32_t, ra2::rmg::Randomizer::kStateSize> rng_state{};
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
