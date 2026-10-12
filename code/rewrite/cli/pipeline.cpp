// 流水线串联 —— 见 pipeline.h。阶段顺序与上下文构造全部对齐各阶段头文件。
// 阶段日志照抄原版 0x598960 的 "RMG: ..."（原版在每次调度前打日志）。
#include "pipeline.h"

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>

#include "rmg/cell_ref.h"
#include "rmg/cliff_work.h"
#include "rmg/corner_lattice.h"
#include "rmg/height_field.h"
#include "rmg/lats.h"
#include "rmg/map_size.h"
#include "rmg/making_regions.h"
#include "rmg/morphable_tiles.h"
#include "rmg/ore_field.h"
#include "rmg/randomizer.h"
#include "rmg/recalc.h"
#include "rmg/rivers.h"
#include "rmg/scaled_random.h"
#include "rmg/start_points.h"
#include "rmg/stage_water.h"
#include "rmg/tech_buildings.h"
#include "rmg/theater.h"
#include "rmg/water_blob.h"
#include "rmg/water_refine.h"
#include "rmg/zones.h"

namespace ra2 {
namespace cli {

namespace {

rmg::MapRect Shrink(const rmg::MapRect& r) {
  // {[0x87F8E4]+4, [0x87F8E8]+4, [0x87F8EC]-8, [0x87F8F0]-8}
  return rmg::MapRect{r.x + 4, r.y + 4, r.w - 8, r.h - 8};
}

struct RngCellCapture {
  PipelineResult* out = nullptr;
  CliWorld* world = nullptr;
};

struct LatDebugCapture {
  PipelineResult* out = nullptr;
  CliWorld* world = nullptr;
};

void CaptureCellsAtRng(void* user, long long calls,
                       const rmg::Randomizer& rng, bool before) {
  (void)rng;
  RngCellCapture* cap = static_cast<RngCellCapture*>(user);
  if (cap == nullptr || cap->out == nullptr || cap->world == nullptr) return;
  PipelineResult* out = cap->out;
  if (before) {
    if (out->capture_before_rng_call_cells < 0 ||
        out->before_rng_call_cells_valid) {
      return;
    }
    if (calls != out->capture_before_rng_call_cells) return;
    out->before_rng_call_cells = cap->world->cells();
    out->before_rng_call_cells_call = calls;
    out->before_rng_call_cells_valid = true;
    return;
  }
  if (out->capture_rng_call_cells < 0 || out->rng_call_cells_valid) return;
  if (calls != out->capture_rng_call_cells) return;
  out->rng_call_cells = cap->world->cells();
  out->rng_call_cells_call = calls;
  out->rng_call_cells_valid = true;
}

void CaptureLatDebugCells(void* user, const char* name,
                          const rmg::LatContext& ctx) {
  LatDebugCapture* cap = static_cast<LatDebugCapture*>(user);
  if (cap == nullptr || cap->out == nullptr || cap->world == nullptr) return;
  if (!cap->out->capture_stage_cells) return;
  StageCellSnapshot s;
  s.name = name;
  s.cells = cap->world->cells();
  if (cap->out->capture_stage_work && ctx.work != nullptr) {
    bool want = cap->out->stage_work_names.empty();
    for (const auto& wanted : cap->out->stage_work_names) {
      if (wanted == name) { want = true; break; }
    }
    if (want) {
      s.work = *ctx.work;
      s.has_work = true;
    }
  }
  cap->out->cell_snapshots.push_back(std::move(s));
  RngCheckpoint cp;
  cp.name = name;
  cp.calls = ctx.rng->call_count();
  cp.index1 = ctx.rng->index1();
  cp.index2 = ctx.rng->index2();
  cp.normal_cached = ctx.normal->has_cached();
  cp.normal_value = ctx.normal->cached();
  cap->out->rng_checkpoints.push_back(cp);
}

}  // namespace

namespace {

// 在一套已建好的 grid/rng/work/world 上跑完整一局——RunPipeline（单局）
// 与 RunPipelineChain（进程内连生）共用的单局体。连生模式下 world/work
// 是上一局的延续（内存态自然残留 = 真实游戏同会话连续生成），rng 每局
// 入口重播种（原版 0x598985 Randomizer::Seed(MapSeed.Seed)）。
bool RunGameOnWorld(const rmg::MapSeedParams& params, unsigned seed,
                    const rmg::TheaterData& theater, const RmgRules& rules,
                    int tile_count, const rmg::MapGrid& grid,
                    rmg::Randomizer* rng_arg,
                    std::vector<rmg::PaintWorkCell>* work_arg,
                    CliWorld* world_arg, PipelineResult* out,
                    std::string* err, bool first_game) {
  if (out == nullptr) return false;
  // 整条流水线在向零截断的 x87 环境下跑（原版 WinMain 全局 RC=chop）。
  rmg::FpuChopScope fpu;
  rmg::Randomizer& rng = *rng_arg;
  std::vector<rmg::PaintWorkCell>& work = *work_arg;
  CliWorld& world = *world_arg;

  // ── 阶段 1 前奏：地图尺寸 ──
  const rmg::MapSize ms =
      rmg::ComputeMapSize(params.width, params.height, params.num_players,
                          params.map_type);
  // grid/world 由调用方按链首局尺寸建；尺寸不一致的局不能复用 world
  // （跨尺寸的格子残留语义未知）。--chain 在 cli_main 已拦，这里是兜底。
  if (grid.width() != ms.width + 4 || grid.height() != ms.height + 12) {
    if (err != nullptr) {
      *err = "chain game map size mismatch: params give grid " +
             std::to_string(ms.width + 4) + "x" +
             std::to_string(ms.height + 12) + " but world grid is " +
             std::to_string(grid.width()) + "x" + std::to_string(grid.height());
    }
    return false;
  }
  rng.Seed(seed);          // 0x598985：每局入口重播种（Seed 完整复位状态）
  rng.reset_call_count();  // 复刻侧观测计数每局清零（原版无此字段）
  // 造山可变形集合（TileTypeClass +0x2E0）：来自 theater ini 各 TileSet
  // 的 Morphable=true 展开（theater.h morphable_tiles），按气候取表——
  // 雪地与温带集合不同（2026-10-07 theater1 根因实锤：温带表用在雪地
  // 会让 Ramp edge fixup 组（可变形）被误判成 Pavement 不可变形）。
  std::set<int> nonmorph;
  {
    const auto& mt = theater.morphable_tiles;
    const int n = tile_count < static_cast<int>(mt.size())
                      ? tile_count
                      : static_cast<int>(mt.size());
    for (int i = 0; i < n; ++i) {
      if (!mt[static_cast<std::size_t>(i)]) nonmorph.insert(i);
    }
  }
  world.set_nonmorph_tiles(std::move(nonmorph));
  world.set_neutral_tech_buildings(rules.neutral_tech_buildings);
  world.set_foundations(rules.foundations);
  world.set_tech_object_registration(true);  // Enable tech building placement
  world.set_tiberium_params(rules.min_tiberium, rules.max_tiberium);
  world.set_max_trees(rules.max_trees);
  world.set_ground_foot(rules.ground_foot);
  world.set_cliff_back_impassability(rules.cliff_back_impassability);
  world.set_overlay_land(rules.overlay_land);
  world.set_terrain_occ_bits(rules.terrain_occ);
  world.set_theater_id(params.theater);
  // 回放模式（--init-cells）：灌入上一局终态 cells 作为本局初始状态。
  // 2026-10-12 修正：原版预览路径每局都向 SetupTheater 传 arg=1（调用点
  // 0x59664c push 1），故每局都进重置子路径清 6 字段——InitCells 不再按
  // first_run 门控。活体 gen#2 tibentry 实测 0 矿格（gen1==gen2==gen3）。
  if (!out->init_cells_path.empty() &&
      !world.LoadInitCells12(out->init_cells_path, err)) {
    return false;
  }
  world.InitCells(first_game && out->init_cells_path.empty());
  std::fprintf(stderr, "RMG: Init random map (W=%d H=%d side=%d seed=%u tile_count=%d)\n",
               ms.width, ms.height, grid.side(), seed, tile_count);

  // 跨阶段共享的流状态
  rmg::NormalDeviate normal(rng);
  int stamp = 0;                 // MapSeed +0x308 块编号
  out->rng_checkpoints.clear();
  out->cell_snapshots.clear();
  out->rng_call_cells_valid = false;
  out->rng_call_cells_call = 0;
  out->rng_call_cells.clear();
  out->before_rng_call_cells_valid = false;
  out->before_rng_call_cells_call = 0;
  out->before_rng_call_cells.clear();
  out->arl_state = ArlStateSnapshot{};
  out->ore_trace.clear();
  out->tech_trace.clear();
  RngCellCapture rng_capture;
  rng_capture.out = out;
  rng_capture.world = &world;
  if (out->capture_rng_call_cells >= 0 ||
      out->capture_before_rng_call_cells >= 0) {
    rng.set_hook(CaptureCellsAtRng, &rng_capture);
  } else {
    // 连生模式 rng 跨局复用：本局不开捕获时必须清掉上一局挂的钩子，
    // 否则 user 指针指向已销毁的栈帧。
    rng.set_hook(nullptr, nullptr);
  }
  const auto mark_rng = [&](const char* name) {
    RngCheckpoint cp;
    cp.name = name;
    cp.calls = rng.call_count();
    cp.index1 = rng.index1();
    cp.index2 = rng.index2();
    cp.normal_cached = normal.has_cached();
    cp.normal_value = normal.cached();
    if (out->capture_stage_rng_state && std::strcmp(name, "after_hills") == 0) {
      for (int i = 0; i < rmg::Randomizer::kStateSize; ++i) {
        cp.state[static_cast<std::size_t>(i)] = rng.state(i);
      }
      cp.has_state = true;
    }
    out->rng_checkpoints.push_back(cp);
  };
  const auto snap_cells = [&](const char* name) {
    if (!out->capture_stage_cells) return;
    StageCellSnapshot s;
    s.name = name;
    s.cells = world.cells();
    if (out->capture_stage_work) {
      bool want = out->stage_work_names.empty();
      for (const auto& wanted : out->stage_work_names) {
        if (wanted == name) { want = true; break; }
      }
      if (want) {
        s.work = work;
        s.has_work = true;
      }
    }
    // ⚠️ 2026-10-04：snap_cells 只有 capture_stage_cells 开时才走。
    // work dump 挂在这下面没问题——但 capture_stage_work 独立。
    out->cell_snapshots.push_back(std::move(s));
  };
  const auto capture_arl = [&]() {
    if (!out->capture_arl_state) return;
    ArlStateSnapshot s;
    s.valid = true;
    s.side = grid.side();
    s.map_w = ms.width;
    s.map_h = ms.height;
    s.region_size = params.region_size;
    s.map_type = params.map_type;
    s.accessibility = params.accessibility;
    s.theater = params.theater;
    s.theater_data = &theater;
    s.rng_calls = rng.call_count();
    s.rng_index1 = rng.index1();
    s.rng_index2 = rng.index2();
    s.normal_cached = normal.has_cached();
    s.normal_value = normal.cached();
    for (int i = 0; i < rmg::Randomizer::kStateSize; ++i) {
      s.rng_state[static_cast<std::size_t>(i)] = rng.state(i);
    }
    s.work = work;
    s.cells = world.cells();
    out->arl_state = std::move(s);
  };
  mark_rng("start");
  // 阶段 1 唯一的取数（0x59A4C7）：25% 抛硬币，决定河上有没有瀑布。
  const bool allow_waterfall = rmg::UniformUnit(rng) < 0.25;
  mark_rng("after_init_random");

  rmg::RegionSet regions;

  // ── 各阶段共享的上下文 ──
  rmg::PaintContext paint;
  paint.grid = grid;
  paint.full_bounds = rmg::Rect{-1, -1, -1, -1};
  paint.work = &work;
  paint.theater = &theater;
  paint.rng = &rng;
  paint.normal = &normal;
  paint.blob_stamp = &stamp;
  paint.cell_level = 4;

  const rmg::MapRect local = world.usable_rect();
  rmg::PlaceParams p;
  p.num_players = params.num_players;
  p.water_amount = params.water_amount;
  p.usable = rmg::Rect{local.x, local.y, local.w, local.h};

  // 阶段 3/4/6/7 共用的 RegionContext（AddRiversAndLakes 也走它）
  rmg::RegionContext rctx;
  rctx.grid = grid;
  rctx.work = &work;
  rctx.world = static_cast<rmg::MakingWorld*>(&world);
  rctx.theater = &theater;
  rctx.rng = &rng;
  rctx.regions = &regions;
  rctx.map_w = ms.width;
  rctx.map_h = ms.height;
  rctx.fallback_level = 4;       // [0xABE2E4] = MapSeed +0x30C（测试同值）
  rctx.normal = &normal;
  rctx.region_size = params.region_size;
  rctx.accessibility = params.accessibility;
  rctx.map_type = params.map_type;
  // ⚠️ 原版 0x578460 IsWithinUsableArea 读的是 MapClass（0x87F7E8）
  // +0xF4(=map_w+4)/+0xFC..+0x108（= Set_LocalSize 输出，未缩放：70×70 →
  // {2,5,70,70}）。阶段 4 的 Clearance / MakeConnections 边界格 / 架桥探条
  const rmg::MapRect usable = local;
  rctx.usable = rmg::Rect{usable.x, usable.y, usable.w, usable.h};
  rctx.map_rect_w = ms.width + 4;

  // ── 阶段 2：铺水（MapType 3/4 不铺水，直接河湖）──
  // 主流程 0x5989xx：MapType ∈ {3,4} 时跳过 FUN_0059a6c0（SeedWater），
  // 只跑 FUN_0059c580（AddRiversAndLakes，河需水量>20、湖总是跑）+ 0x59c630。
  rmg::RiverParams river_p;
  river_p.map_type = params.map_type;
  river_p.water_amount = params.water_amount;
  river_p.painted_total = 0;     // +0x304 只在这两个函数里自增
  river_p.blob_id = stamp;       // +0x308，接阶段 2 的块编号
  river_p.cell_level = 4;
  river_p.allow_waterfall = allow_waterfall;
  std::fprintf(stderr, "RMG: Seeding water\n");
  if (params.map_type == 3 || params.map_type == 4) {
    if (params.water_amount > 20) {
      world.SetShorePaintFlag(false);  // 湖/瀑布收尾 Shoreline(blob, 0)
      rmg::AddRiversAndLakes(rctx, world, river_p);
      world.SetShorePaintFlag(true);   // 恢复涂块收尾 Shoreline(blob, 1)
    }
  } else {
    rmg::SeedWater(paint, world, p, params.map_type);
  }
  // River/WaterBlob 共享 MapSeedClass +0x30C；无瀑布且峡谷成功时，原版在
  // 阶段 2 收尾把它从 4 提到 8，后续 InitRegions/MakeRegions 继续读取新值。
  rctx.fallback_level = river_p.cell_level;
  mark_rng("after_seeding_water");
  snap_cells("after_seeding_water");

  // ── 水面细化 ──
  std::fprintf(stderr, "RMG: RefineWaterTiles\n");
  rmg::RefineWaterTiles(world, grid, theater, rng);
  mark_rng("after_refine_water");
  snap_cells("after_refine_water");

  // ── 阶段 3：区域 ──
  std::fprintf(stderr, "RMG: Init regions\n");
  rmg::InitRegions(rctx, params.map_type);
  mark_rng("after_init_regions");
  snap_cells("after_init_regions");
  // ── 阶段 4：造坡道/架桥/悬崖（MapType 3/4）──
  if (params.map_type == 3 || params.map_type == 4) {
    std::fprintf(stderr, "RMG: Making regions\n");
    rmg::RebuildAndSplitRegions(rctx);
    mark_rng("after_rebuild_and_split_regions");
    snap_cells("after_rebuild_and_split_regions");
    rmg::RebuildRegions(rctx, world);
    mark_rng("after_rebuild_regions");
    snap_cells("after_rebuild_regions");
    rmg::TrimCliffCorners(rctx, world);
    mark_rng("after_trim_cliff_corners");
    snap_cells("after_trim_cliff_corners");
    rmg::RepaintAll(rctx, world);
    mark_rng("after_repaint_all");
    snap_cells("after_repaint_all");
    rmg::RerollMultitileCliffs(rctx, world);
    mark_rng("after_reroll_multitile_cliffs");
    snap_cells("after_reroll_multitile_cliffs");
  }
  snap_cells("after_making_regions");
  mark_rng("after_making_regions");

  // ⚠️ 2026-10-07 复核：这里原本有一次「额外全图 Recalc」，注释声称对应
  // 原版 0x598B8E-0x598C0A——但那段反汇编只有预览/版本门调用（0x641140/
  // 0x5E7EB0），原版在 making_regions 与 SpreadGreen(0x59B740) 之间
  // **没有** RecalcAttributes（主流程里 RecalcAttributes 直调点只有
  // 0x598E48/0x598FE7/0x599153/0x59937D 四处循环）。这次额外 Recalc 是
  // 当时为修 seed1 的 673 格 iso 差异加的补偿；agent-1 的 RecalcLandType
  // 全分支重写（0x483C80）落地后，mt0(MapType=0) 实证它反而是分岔源
  // （真实首次 Recalc 在 SpreadGreen 之后 0x598E31）。已移除。
  //
  // 若未来 seed1 回归出 673 格 iso 差异，说明上游阶段（making_regions
  // 尾部 0x5A17F0 或更早）仍有未还原的写砖行为，去修上游，别加回来。

  // ── 阶段 5：绿地扩张 + 逐格重算属性 ──
  std::fprintf(stderr, "RMG: SpreadGreen\n");
  // 实机哑元 0xABDC50 的 +0x38 在进 SpreadGreen **前**是 0：第一遍前沿收集
  // 会把出图邻收进去，某轮 pick 抽中后 WriteTile 把它写成 GreenTile(493)。
  // 之后 Recalc 的 SetupLAT 段 3 读出图邻时就拿到 493（认作同组、不置过渡位）。
  // 所以 CLI 不在这里预置残留（预置会让前沿收集直接跳过出图邻 → 取数分岔），
  // 而在 SpreadGreen 结束后、Recalc 前补上实机同款残留。
  rmg::SpreadGreen(world, grid, theater, rng);
  snap_cells("after_spread_green_raw");
  // 2026-10-10 修正：实机哑元 0xABDC50 的 +0x38 在 LAT 段 3 时刻为 **0**
  // （真值 dump 出图格 iso=0 实锤；且实机靠哑元=0 置出过渡位 m=9 → 503）。
  // 此前「实机哑元=493」结论错误，该钉死已移除——哑元残留必须与实机一致为 0，
  // 否则 ClearToGreenLat 会把出图邻误判为同组（m 少置位 → 少产过渡砖）。
  // 原版 0x598E31-0x598E4D 走 CellClass::RecalcAttributes(-1)。CliWorld 的
  // 版本比算法库的 SmoothWorld 版多更新 +0xEC/+0x4C（阶段 6 的输入），
  // 所以这里不调 RecalcAttributesPass，改逐格走 world 的完整版。
  // 主流程在「铺绿/布矿/造山前/LAT 后」共 Recalc 4 次（rmg_pipeline_decomp.txt
  // 行 214/283/330/409），CLI 在对应位置各补一次。
  const auto recalc_pass = [&]() {
    grid.ForEachCell(
        [&](const rmg::CellCoord& c) { world.RecalcAttributes(c, -1); });
  };
  recalc_pass();
  snap_cells("after_spread_green_recalc");
  // 把每格的地面类别/层高喂进寻路地形表（原版 RecalcAttributes 收尾的
  // `mov [ebx], dl / mov [ebx+1], al`）。
  rmg::ZoneTable zt(grid.width(), grid.height());
  // ⚠️ 原版阶段 5 喂给 0x56C510 的 zone 表就是 **RecalcAttributes 的 +0x4C**
  // 现成输出（MapClass::Update_Pathfinding_1 直接读 CellClass+0x4C，见
  // movement-zones.md）。它同时写 land **和** level：level 也进了表（权威
  // beforerebuild/zone_table_4b.bin 实测 level 分布 {0:12595, 4:6827,
  // 5:10, 6:10, 7:18, 8:5189}，与阶段 5 Recalc 时各格 +0x11B 一致）。
  // 这里喂 land=+0x4C、level=+0x11B，两列都照抄原版。
  grid.ForEachCell([&](const rmg::CellCoord& c) {
    const CliCell& cell = world.CellAt(c);
    rmg::ZoneCell& zc = zt.at(c.x, c.y);
    zc.land = cell.zoneId;
    zc.level = static_cast<std::uint8_t>(cell.level);
  });
  if (out->capture_stage_cells) {
    out->zone_table_side = zt.side();
    out->zone_table_beforerebuild.assign(zt.begin(), zt.end());
  }
  mark_rng("after_spread_green");

  // ── 阶段 6：出生点 ──
  std::fprintf(stderr, "RMG: Creating starting points\n");
  rmg::StartPointContext sp;
  sp.grid = grid;
  sp.side = grid.side();
  sp.work = &work;
  sp.regions = &regions;
  sp.rng = &rng;
  sp.world = &world;
  sp.num_players = params.num_players;
  sp.map_type = params.map_type;
  sp.waypoint_scale = params.tiberium_layout;   // MapSeed +0x58
  sp.map_w = ms.width;
  sp.map_h = ms.height;
  sp.map_rect_w = ms.width + 4;
  sp.map_rect_h = ms.height + 12;   // MapClass +0xF8（stage-init.md：H+12）
  sp.theater = &theater;
  sp.usable = usable;
  sp.trace = &out->start_trace;
  rmg::ZoneResult zone_result;
  sp.zone_result_out = &zone_result;
  rmg::ComputeStartZoneSelection(sp, zt);
  world.SetZoneData(&zt, &zone_result);
  rmg::PlaceStartPoints(sp);
  rmg::MarkStartInfluence(sp);
  mark_rng("after_start_points");
  snap_cells("after_start_points");

  // ── 阶段 7：中立科技建筑（MapType ≠ 0）──
  if (params.map_type != 0) {
    std::fprintf(stderr, "RMG: Adding tech buildings\n");
    rmg::TechContext tc;
    tc.side = grid.side();
    tc.work = &work;
    tc.regions = &regions;
    tc.rng = &rng;
    tc.world = &world;
    tc.map_type = params.map_type;
    tc.type_names = &rules.neutral_tech_buildings;
    tc.trace = &out->tech_trace;
    rmg::AddTechBuildings(tc);
  }
  mark_rng("after_tech_buildings");
  snap_cells("after_tech_buildings");
  capture_arl();

  // ── 阶段 8：布矿 ──
  std::fprintf(stderr, "RMG: Creating tiberium\n");
  // ⚠️ 2026-10-03 曾试过把 regions.Reset() 移到 PlaceTiberium 之前（v11），
  // 结果 after_tiberium 0 次 RNG 消耗、最终 calls=124624 ≠ 权威 129278——错。
  // 权威 at_5A23A0 快照 RNG=(161,14)，游戏最终 (28,131)=129278；
  // CLI v9 消耗 67617 次（61661→129278）与游戏完全一致，含 tiberium 的 7720 次。
  // 游戏实际**执行**了 tiberium（[0xABDFA0] 在入口 ≠ 0），跑完后才清空区域表。
  rmg::OreContext oc;
  oc.grid = grid;
  oc.work = &work;
  oc.world = &world;
  oc.rng = &rng;
  oc.normal = &normal;
  oc.side = grid.side();
  oc.map_w = ms.width + 4;
  oc.local = local;
  oc.trace = &out->ore_trace;
  oc.pop_trace = &out->ore_pop_trace;
  oc.enq_trace = &out->ore_enq_trace;
  oc.ore_work_dump_prefix = out->ore_work_dump_prefix;
  rmg::TiberiumParams tp;
  tp.map_type = params.map_type;
  tp.resources = params.resources;
  tp.tiberium = params.tiberium;
  tp.min_tiberium = rules.min_tiberium;
  tp.max_tiberium = rules.max_tiberium;
  rmg::PlaceTiberium(oc, regions, tp);
  mark_rng("after_tiberium");
  snap_cells("after_tiberium");

  // 原版在布矿返回后立即清空工作数组的区域/排队字段，并销毁区域表。
  // 这是 0x598960 调度中紧接 FUN_005A23A0 的整表清理，不属于布矿输入。
  for (rmg::PaintWorkCell& cell : work) {
    cell.blob_id = -1;
    cell.queued = -1;
  }
  regions.Reset();

  // 原版布矿后 Recalc 第二次（rmg_pipeline_decomp.txt 行 283），造山前还有
  // 第三次（行 330）。
  recalc_pass();
  recalc_pass();
  snap_cells("after_tiberium_recalc");

  // ── 阶段 9：造山 ──
  std::fprintf(stderr, "RMG: Creating hills\n");
  rmg::HillContext hc;
  hc.grid = grid;
  hc.work = &work;
  hc.world = &world;
  hc.theater = &theater;
  hc.ruggedness = params.ruggedness;
  hc.nd = &normal;
  rmg::MarkHeightAnchors(*hc.world, hc.grid, *hc.theater, *hc.work);
  mark_rng("after_mark_height_anchors");
  snap_cells("after_mark_height_anchors");
  rmg::GenerateHeightField(hc.grid, *hc.work, hc.ruggedness, *hc.nd);
  mark_rng("after_generate_height_field");
  snap_cells("after_generate_height_field");
  rmg::BuildCornerLattice(hc);
  mark_rng("after_build_corner_lattice");
  snap_cells("after_build_corner_lattice");
  rmg::RaiseAllCells(hc);
  mark_rng("after_raise_all_cells");
  snap_cells("after_raise_all_cells");
  rmg::ApplyLatticeToCells(hc);
  mark_rng("after_apply_lattice");
  snap_cells("after_apply_lattice");
  rmg::FlattenRampBlocks(hc);
  mark_rng("after_flatten_ramp_blocks");
  snap_cells("after_flatten_ramp_blocks");
  // ⚠️ 2026-10-04 探针：LAT 输入快照（供 emu 对拍；capture_stage_cells 开时
  // 自动落 cells_after_lat_input.bin）
  snap_cells("after_lat_input");
  mark_rng("after_hills");

  // ── 阶段 10：地表装饰 ──
  std::fprintf(stderr, "RMG: Creating LATs, rocks etc\n");
  rmg::LatContext lc;
  lc.grid = grid;
  lc.work = &work;
  lc.world = &world;
  lc.rng = &rng;
  lc.normal = &normal;
  lc.side = grid.side();
  lc.theater = &theater;
  lc.params.theater = params.theater;
  lc.params.vegetation = params.vegetation;
  // 原版树林预算读 MapSeedClass+0x64 = .sed 的 Width **档位**（0..3），
  // 不是 ComputeMapSize 算出的地图宽（stage-init.md 三之二实锤档位语义）。
  lc.params.width = params.width;
  lc.params.max_trees = rules.max_trees;
  // 原版 0x42B1F0 读 [0x87F8DC]=MapRect.W = W+4 / [0x87F8E0]=H+12
  // （阶段 1 在 0x599D48 Set_Dimensions({0,0,W+4,H+12}) 实锤）。
  lc.params.map_rect_w = ms.width + 4;
  lc.params.map_rect_h = ms.height + 12;
  LatDebugCapture lat_capture;
  lat_capture.out = out;
  lat_capture.world = &world;
  lc.debug_hook = CaptureLatDebugCells;
  lc.debug_user = &lat_capture;
  rmg::CreateLats(lc);
  mark_rng("after_lats");
  snap_cells("after_lats");

  // 原版 LAT 后 Recalc 第四次（rmg_pipeline_decomp.txt 行 409）。
  recalc_pass();
  snap_cells("after_lats_recalc");

  // RMG 收尾的矿帧平滑。2026-10-06 活体取证全链闭合：
  //   调用点 = 0x59944C `call 0x568BB0(MapClass, 1)`（rmg_done 窗口内，
  //   硬件监视点+断点二分实锤：0x59943F 处 od 未变、0x599451 处已变）；
  //   0x568BB0 的逐格循环在 0x568DD8 对矿格调 **0x4818E0**；
  //   0x4818E0 数 8 邻域同 TiberiumIndex 格数（0x5FDD20 比对），然后
  //   `idiv [type+0xE4]` 取余，od = dword 表 [0x81CD28 + 余数*4] 的低字节。
  //   表前 9 项 = {0,1,3,4,6,7,8,10,11}，与活体拟合的 (pc*11+4)>>3 完全一致
  //   （316/316 格零例外），iso/sub/lv/ramp/ov 均不变，不耗主 RNG。
  static const int kOdFrameTable[9] = {0, 1, 3, 4, 6, 7, 8, 10, 11};
  grid.ForEachCell([&](const rmg::CellCoord& c) {
    const int ov = world.Overlay(c);
    int lo, hi;
    if (ov >= 27 && ov <= 38) {
      lo = 27;
      hi = 38;
    } else if (ov >= 102 && ov <= 113) {
      lo = 102;
      hi = 113;
    } else {
      return;
    }
    int pc = 0;
    for (int d = 0; d < 8; ++d) {
      const rmg::CellCoord n{
          static_cast<std::int16_t>(c.x + rmg::kAdjacentDelta[d].x),
          static_cast<std::int16_t>(c.y + rmg::kAdjacentDelta[d].y)};
      const int nov = world.Overlay(n);   // 出图格 dummy overlay=-1，不计入
      if (nov >= lo && nov <= hi) ++pc;
    }
    world.SetOverlayData(c, kOdFrameTable[pc]);
  });
  snap_cells("after_ore_frame_smooth");

  // ── 收集输出 ──
  out->map_w = ms.width;
  out->map_h = ms.height;
  out->cells = world.cells();
  out->exists = world.exists();
  out->waypoints = world.waypoints();
  out->terrains = world.terrains();
  out->buildings = world.buildings();
  out->start_locations = world.start_locations();
  out->rng_calls = rng.call_count();
  out->rng_index1 = rng.index1();
  out->rng_index2 = rng.index2();
  for (int i = 0; i < rmg::Randomizer::kStateSize; ++i) {
    out->rng_state[static_cast<std::size_t>(i)] = rng.state(i);
  }

  if (err != nullptr) err->clear();
  return true;
}

}  // namespace

bool RunPipeline(const rmg::MapSeedParams& params, unsigned seed,
                 const rmg::TheaterData& theater,
                 const std::vector<rmg::TileType>& tiles,
                 const RmgRules& rules, int tile_count, PipelineResult* out,
                 std::string* err) {
  if (out == nullptr) return false;
  const rmg::MapSize ms =
      rmg::ComputeMapSize(params.width, params.height, params.num_players,
                          params.map_type);
  const rmg::MapGrid grid(ms.width + 4, ms.height + 12);
  const std::size_t n =
      static_cast<std::size_t>(grid.side()) * static_cast<std::size_t>(grid.side());
  std::vector<rmg::PaintWorkCell> work(n);
  rmg::Randomizer rng;   // RunGameOnWorld 入口 Seed(seed)，与先播种等价
  CliWorld world(grid, theater, tiles, rng, work, tile_count);
  return RunGameOnWorld(params, seed, theater, rules, tile_count, grid, &rng,
                        &work, &world, out, err, /*first_game=*/true);
}

bool RunPipelineChain(const std::vector<ChainGame>& games,
                      const rmg::TheaterData& theater,
                      const std::vector<rmg::TileType>& tiles,
                      const RmgRules& rules, int tile_count,
                      std::vector<PipelineResult>* results, std::string* err,
                      bool capture_stage_cells, bool fresh_per_game) {
  if (results == nullptr || games.empty() ||
      results->size() != games.size()) {
    if (err != nullptr) *err = "RunPipelineChain: bad arguments";
    return false;
  }
  const rmg::MapSize ms0 =
      rmg::ComputeMapSize(games[0].params.width, games[0].params.height,
                          games[0].params.num_players, games[0].params.map_type);
  const rmg::MapGrid grid(ms0.width + 4, ms0.height + 12);
  const std::size_t n =
      static_cast<std::size_t>(grid.side()) * static_cast<std::size_t>(grid.side());
  if (fresh_per_game) {
    // 干净模式批量：每局一套全新 grid/rng/work/world == 独立进程单局
    // （干净变体历史无关——游戏侧 T0/T1 交叉实验实锤），免去进程重启。
    for (std::size_t i = 0; i < games.size(); ++i) {
      const rmg::MapSize ms_i = rmg::ComputeMapSize(
          games[i].params.width, games[i].params.height,
          games[i].params.num_players, games[i].params.map_type);
      const rmg::MapGrid gdim(ms_i.width + 4, ms_i.height + 12);
      const std::size_t ni = static_cast<std::size_t>(gdim.side()) *
                             static_cast<std::size_t>(gdim.side());
      std::vector<rmg::PaintWorkCell> wk(ni);
      rmg::Randomizer rng_i;
      CliWorld world_i(gdim, theater, tiles, rng_i, wk, tile_count);
      (*results)[i].capture_stage_cells = capture_stage_cells;
      if (!RunGameOnWorld(games[i].params, games[i].seed, theater, rules,
                          tile_count, gdim, &rng_i, &wk, &world_i,
                          &(*results)[i], err, /*first_game=*/true)) {
        return false;
      }
    }
    return true;
  }
  // 首局建 grid/rng/work/world；第 2 局起全部复用——CliWorld 内存态
  // （cells + terrains_ 记录 + 哑元 + loaded_tiles_ …）自然延续，
  // 与真实游戏同会话连续生成同构。
  std::vector<rmg::PaintWorkCell> work(n);
  rmg::Randomizer rng;
  CliWorld world(grid, theater, tiles, rng, work, tile_count);
  for (std::size_t i = 0; i < games.size(); ++i) {
    if (i > 0) {
      const rmg::MapSize ms_i = rmg::ComputeMapSize(
          games[i].params.width, games[i].params.height,
          games[i].params.num_players, games[i].params.map_type);
      if (ms_i.width != ms0.width || ms_i.height != ms0.height) {
        if (err != nullptr) {
          *err = "chain game #" + std::to_string(i + 1) +
                 " map size differs from game #1 (chain requires equal sizes)";
        }
        return false;
      }
    }
    (*results)[i].capture_stage_cells = capture_stage_cells;
    if (!RunGameOnWorld(games[i].params, games[i].seed, theater, rules,
                        tile_count, grid, &rng, &work, &world,
                        &(*results)[i], err, /*first_game=*/i == 0)) {
      return false;
    }
  }
  return true;
}

}  // namespace cli
}  // namespace ra2
