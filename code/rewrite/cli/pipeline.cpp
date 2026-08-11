// 流水线串联 —— 见 pipeline.h。阶段顺序与上下文构造全部对齐各阶段头文件。
// 阶段日志照抄原版 0x598960 的 "RMG: ..."（原版在每次调度前打日志）。
#include "pipeline.h"

#include <cstdio>

#include "rmg/corner_lattice.h"
#include "rmg/lats.h"
#include "rmg/map_size.h"
#include "rmg/making_regions.h"
#include "rmg/ore_field.h"
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

}  // namespace

bool RunPipeline(const rmg::MapSeedParams& params, unsigned seed,
                 const rmg::TheaterData& theater,
                 const std::vector<rmg::TileType>& tiles,
                 const RmgRules& rules, int tile_count, PipelineResult* out,
                 std::string* err) {
  if (out == nullptr) return false;
  // 整条流水线在向零截断的 x87 环境下跑（原版 WinMain 全局 RC=chop）。
  rmg::FpuChopScope fpu;

  // ── 阶段 1 前奏：地图尺寸 ──
  const rmg::MapSize ms =
      rmg::ComputeMapSize(params.width, params.height, params.num_players,
                          params.map_type);
  const rmg::MapGrid grid(ms.width + 4, ms.height + 12);
  const std::size_t n =
      static_cast<std::size_t>(grid.side()) * static_cast<std::size_t>(grid.side());
  std::vector<rmg::PaintWorkCell> work(n);
  rmg::Randomizer rng(seed);
  CliWorld world(grid, theater, tiles, rng, work, tile_count);
  world.set_neutral_tech_buildings(rules.neutral_tech_buildings);
  world.set_tiberium_params(rules.min_tiberium, rules.max_tiberium);
  world.set_max_trees(rules.max_trees);
  world.set_ground_foot(rules.ground_foot);
  world.InitCells();
  std::fprintf(stderr, "RMG: Init random map (W=%d H=%d side=%d seed=%u)\n",
               ms.width, ms.height, grid.side(), seed);

  // 阶段 1 唯一的取数（0x59A4C7）：25% 抛硬币，决定河上有没有瀑布。
  const bool allow_waterfall = rmg::UniformUnit(rng) < 0.25;

  // 跨阶段共享的流状态
  rmg::NormalDeviate normal(rng);
  int stamp = 0;                 // MapSeed +0x308 块编号
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
  const rmg::MapRect usable = Shrink(local);
  rctx.usable = rmg::Rect{usable.x, usable.y, usable.w, usable.h};
  rctx.map_rect_w = ms.width + 4;

  // ── 阶段 2：铺水（+ 阶段 2.5 河湖）──
  std::fprintf(stderr, "RMG: Seeding water\n");
  rmg::SeedWater(paint, world, p, params.map_type);

  rmg::RiverParams river_p;
  river_p.map_type = params.map_type;
  river_p.water_amount = params.water_amount;
  river_p.painted_total = 0;     // +0x304 只在这两个函数里自增
  river_p.blob_id = stamp;       // +0x308，接阶段 2 的块编号
  river_p.cell_level = 4;
  river_p.allow_waterfall = allow_waterfall;
  if ((params.map_type == 3 || params.map_type == 4) &&
      params.water_amount > 20) {
    std::fprintf(stderr, "RMG: AddRiversAndLakes\n");
    rmg::AddRiversAndLakes(rctx, world, river_p);
  }

  // ── 水面细化 ──
  std::fprintf(stderr, "RMG: RefineWaterTiles\n");
  rmg::RefineWaterTiles(world, grid, theater, rng);

  // ── 阶段 3：区域 ──
  std::fprintf(stderr, "RMG: Init regions\n");
  rmg::InitRegions(rctx, params.map_type);

  // ── 阶段 4：造坡道/架桥/悬崖（MapType 3/4）──
  if (params.map_type == 3 || params.map_type == 4) {
    std::fprintf(stderr, "RMG: Making regions\n");
    rmg::MakeRegions(rctx, world, params.map_type);
  }

  // ── 阶段 5：绿地扩张 + 逐格重算属性 ──
  std::fprintf(stderr, "RMG: SpreadGreen\n");
  rmg::SpreadGreen(world, grid, theater, rng);
  // 原版 0x598E31-0x598E4D 走 CellClass::RecalcAttributes(-1)。CliWorld 的
  // 版本比算法库的 SmoothWorld 版多更新 +0xEC/+0x4C（阶段 6 的输入），
  // 所以这里不调 RecalcAttributesPass，改逐格走 world 的完整版。
  grid.ForEachCell([&](const rmg::CellCoord& c) { world.RecalcAttributes(c, -1); });
  // 把每格的地面类别/层高喂进寻路地形表（原版 RecalcAttributes 收尾的
  // `mov [ebx], dl / mov [ebx+1], al`）。
  rmg::ZoneTable zt(grid.width(), grid.height());
  grid.ForEachCell([&](const rmg::CellCoord& c) {
    const CliCell& cell = world.CellAt(c);
    rmg::ZoneCell& zc = zt.at(c.x, c.y);
    zc.land = cell.zoneId;
    zc.level = static_cast<std::uint8_t>(cell.level);
  });

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
  sp.theater = &theater;
  sp.usable = usable;
  rmg::ZoneResult zone_result;
  sp.zone_result_out = &zone_result;
  rmg::ComputeStartZoneSelection(sp, zt);
  world.SetZoneData(&zt, &zone_result);
  rmg::PlaceStartPoints(sp);

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
    rmg::AddTechBuildings(tc);
  }

  // ── 阶段 8：布矿 ──
  std::fprintf(stderr, "RMG: Creating tiberium\n");
  rmg::OreContext oc;
  oc.grid = grid;
  oc.work = &work;
  oc.world = &world;
  oc.rng = &rng;
  oc.normal = &normal;
  oc.side = grid.side();
  oc.map_w = ms.width + 4;
  oc.local = local;
  rmg::TiberiumParams tp;
  tp.map_type = params.map_type;
  tp.resources = params.resources;
  tp.tiberium = params.tiberium;
  tp.min_tiberium = rules.min_tiberium;
  tp.max_tiberium = rules.max_tiberium;
  rmg::PlaceTiberium(oc, regions, tp);

  // ── 阶段 9：造山 ──
  std::fprintf(stderr, "RMG: Creating hills\n");
  rmg::HillContext hc;
  hc.grid = grid;
  hc.work = &work;
  hc.world = &world;
  hc.theater = &theater;
  hc.ruggedness = params.ruggedness;
  hc.nd = &normal;
  rmg::CreateHills(hc);

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
  lc.params.width = params.width;
  lc.params.max_trees = rules.max_trees;
  lc.params.map_rect_w = ms.width + 4;
  lc.params.map_rect_h = ms.height + 12;
  rmg::CreateLats(lc);

  // ── 收集输出 ──
  out->map_w = ms.width;
  out->map_h = ms.height;
  out->cells = world.cells();
  out->exists = world.exists();
  out->waypoints = world.waypoints();
  out->terrains = world.terrains();
  out->buildings = world.buildings();
  out->start_locations = world.start_locations();

  if (err != nullptr) err->clear();
  return true;
}

}  // namespace cli
}  // namespace ra2
