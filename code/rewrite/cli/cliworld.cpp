// CliWorld 实现——见 cliworld.h 头部注释。
//
// 一份格子状态 + 一份工作数组，同时喂给流水线全部阶段。菱形外的格子统一退到
// 共享哑元 dummy_（原版 0xABDC50 语义），上一次留在哑元里的字段会被下一次读到。
#include "cliworld.h"

#include <algorithm>
#include <cstring>

#include "rmg/place_tile.h"
#include "rmg/recalc.h"
#include "rmg/regions.h"
#include "rmg/scaled_random.h"
#include "rmg/shore_mask.h"
#include "rmg/shore_smooth.h"
#include "rmg/zones.h"

namespace ra2 {
namespace cli {

CliWorld::CliWorld(const ra2::rmg::MapGrid& grid,
                   const ra2::rmg::TheaterData& theater,
                   const std::vector<ra2::rmg::TileType>& tiles,
                   ra2::rmg::Randomizer& rng,
                   std::vector<ra2::rmg::PaintWorkCell>& work, int tile_count)
    : grid_(grid),
      theater_(theater),
      rng_(rng),
      work_(work),
      cells_(static_cast<std::size_t>(kCellSlots)),
      exists_(static_cast<std::size_t>(kCellSlots), 0),
      mask_cache_(work.size(), -1),
      tile_count_(tile_count) {
  for (const ra2::rmg::TileType& t : tiles) tiles_[t.tile_number] = t;
  // 可用区矩形：MapClass::Set_LocalSize({2,5,W,H}) 与整图矩形求交后夹取。
  // 整图矩形 = {0,0,W+4,H+12} = grid 的宽高。
  const ra2::rmg::MapRect want{2, 5, grid.width() - 4, grid.height() - 12};
  local_ = ra2::rmg::ClampLocalRect(want, grid.width(), grid.height());
  grid_.ForEachCell([&](const ra2::rmg::CellCoord& c) {
    exists_[static_cast<std::size_t>(SlotOf(c))] = 1;
  });
  // 出图格子的哑元残留必须可读（原版 0xABDC50 是全 exe 共享的静态对象，
  // 阶段 1 清格子用的是迭代器，从不碰它；这里用构造时的默认值）。
}

void CliWorld::InitCells() {
  // 阶段 1 全图重置（stage-init.md §四）——只走菱形内的格。
  grid_.ForEachCell([&](const ra2::rmg::CellCoord& c) {
    CliCell& cell = Mut(c);
    cell.ramp = 0;         // +0x11C
    cell.level = 4;        // +0x11B = 常数 4
    cell.isoTile = 0;      // +0x38
    cell.subtile = 0;      // +0x11A
    cell.overlay = -1;     // +0x44 = 0xFFFFFFFF
    cell.overlayData = 0;  // +0x11E
    cell.ground = 0;       // +0xEC
    cell.zoneId = 0;       // +0x4C
    cell.object = 0;       // +0xE4
  });
  // 工作数组（stage-init.md §五）：全零，但 +0x4A = 1（有效位）。
  for (ra2::rmg::PaintWorkCell& w : work_) {
    w = ra2::rmg::PaintWorkCell{};
    w.flag4a = true;
  }
  // +0x40 掩码缓存整表失效（-1）
  mask_cache_.assign(mask_cache_.size(), -1);
  // 再扫一遍：打包坐标写进记录头 4 字节，+0x11B 改成 MapSeed +0x30C（= 4）。
  grid_.ForEachCell([&](const ra2::rmg::CellCoord& c) {
    work_[WorkIdx(c)].coord = c;
    Mut(c).level = 4;
  });
  // 输出记录复位
  terrains_.clear();
  buildings_.clear();
  start_locations_.clear();
  waypoints_.clear();
  active_buildings_.clear();
}

// ── 槽位 / 工作数组下标 / 哑元退让 ──────────────────────────────────────

int CliWorld::SlotOf(const ra2::rmg::CellCoord& c) const {
  const int idx = (c.y << 9) + c.x;
  return (idx < 0 || idx >= kCellSlots) ? -1 : idx;
}

std::size_t CliWorld::WorkIdx(const ra2::rmg::CellCoord& c) const {
  const long long i = static_cast<long long>(c.y) * grid_.side() + c.x;
  if (i < 0) return 0;
  if (i >= static_cast<long long>(work_.size())) return work_.size() - 1;
  return static_cast<std::size_t>(i);
}

const CliCell& CliWorld::At(const ra2::rmg::CellCoord& c) const {
  const int s = SlotOf(c);
  return (s < 0 || exists_[static_cast<std::size_t>(s)] == 0) ? dummy_
                                                              : cells_[s];
}

CliCell& CliWorld::Mut(const ra2::rmg::CellCoord& c) {
  const int s = SlotOf(c);
  return (s < 0 || exists_[static_cast<std::size_t>(s)] == 0) ? dummy_
                                                              : cells_[s];
}

// ── PaintWorld ──────────────────────────────────────────────────────────

bool CliWorld::IsPaintable(const ra2::rmg::CellCoord& c) const {
  // 0x4865D0：岸砖 / 水砖 / 四组瀑布。逐条照抄，不加 -1 保护。
  return ra2::rmg::IsWaterOrShoreTile(IsoTile(c), theater_);
}

int CliWorld::IsoTile(const ra2::rmg::CellCoord& c) const {
  const int s = SlotOf(c);
  // ⚠️ 原版 CellClass +0x38 是无符号 16 位（0x47D2B0 里 movzx 读）。
  // 0xFFFF 必须读回 65535，不能符号扩展成 -1——RecalcAttributes 的
  // `tile >= tile_count` / `tile == 0xFFFF` 两条判定都靠它。
  return (s < 0 || exists_[static_cast<std::size_t>(s)] == 0)
             ? ra2::rmg::ShoreWorld::kNoCell
             : static_cast<std::int32_t>(
                   static_cast<std::uint16_t>(
                       cells_[static_cast<std::size_t>(s)].isoTile));
}

void CliWorld::SetIsoTile(const ra2::rmg::CellCoord& c, int v) {
  Mut(c).isoTile = static_cast<std::int16_t>(v);
}

void CliWorld::SetCellSubTile(const ra2::rmg::CellCoord& c, int v) {
  Mut(c).subtile = static_cast<std::int8_t>(v);
}

void CliWorld::SetCellLevel(const ra2::rmg::CellCoord& c, int v) {
  Mut(c).level = static_cast<std::int8_t>(v);
}

bool CliWorld::SmoothShoreline(int blob_id) {
  // 0x57A0C0(blobId, 0)：湖收尾的岸线平滑 flag 是 0，不是涂块收尾（0x59C433）
  // 传的 1。flag 为真时贴砖器会关掉工作数组读写、归属恒判「同块」，岸砖就
  // 不会登记归属——湖的 Dilate 隔离检测会因此撞上「无主岸砖」而整块失败。
  return ra2::rmg::ShorelineSmooth(*this, grid_, theater_, rng_, blob_id,
                                   false);
}

// ── ShoreWorld ──────────────────────────────────────────────────────────

bool CliWorld::WorkValid(const ra2::rmg::CellCoord& c) const {
  return work_[WorkIdx(c)].flag4a;
}

int CliWorld::MaskCache(const ra2::rmg::CellCoord& c) const {
  return mask_cache_[WorkIdx(c)];
}

// ── TileWorld ───────────────────────────────────────────────────────────

bool CliWorld::CellExists(const ra2::rmg::CellCoord& c) const {
  const int s = SlotOf(c);
  return s >= 0 && exists_[static_cast<std::size_t>(s)] != 0;
}

int CliWorld::SubTile(const ra2::rmg::CellCoord& c) const {
  return At(c).subtile;
}

void CliWorld::SetSubTile(const ra2::rmg::CellCoord& c, int v) {
  Mut(c).subtile = static_cast<std::int8_t>(v);
}

void CliWorld::SetLevel(const ra2::rmg::CellCoord& c, int v) {
  Mut(c).level = static_cast<std::int8_t>(v);
}

void CliWorld::SetRamp(const ra2::rmg::CellCoord& c, int v) {
  Mut(c).ramp = static_cast<std::int8_t>(v);
}

void CliWorld::SetDummyCoord(const ra2::rmg::CellCoord& c) {
  dummy_coord_ = c;
}

int CliWorld::DummyIsoTile() const { return dummy_.isoTile; }

int CliWorld::DummySubTile() const { return dummy_.subtile; }

void CliWorld::SetDummyTile(int tile, int subtile, int level) {
  dummy_.isoTile = static_cast<std::int16_t>(tile);
  dummy_.subtile = static_cast<std::int8_t>(subtile);
  dummy_.level = static_cast<std::int8_t>(level);
}

void CliWorld::SetDummyIsoTile(int tile) {
  dummy_.isoTile = static_cast<std::int16_t>(tile);
}

int CliWorld::WorkBlob(const ra2::rmg::CellCoord& c) const {
  return work_[WorkIdx(c)].blob_id;
}

void CliWorld::SetWorkBlob(const ra2::rmg::CellCoord& c, int v) {
  work_[WorkIdx(c)].blob_id = v;
}

// ── SmoothWorld ─────────────────────────────────────────────────────────

void CliWorld::SetMaskCache(const ra2::rmg::CellCoord& c, int v) {
  mask_cache_[WorkIdx(c)] = v;
}

void CliWorld::ResetMaskCaches() {
  mask_cache_.assign(mask_cache_.size(), -1);
}

int CliWorld::Level(const ra2::rmg::CellCoord& c) const { return At(c).level; }

ra2::rmg::CellCoord CliWorld::DummyCoord() const { return dummy_coord_; }

const ra2::rmg::TileType* CliWorld::TileByIndex(int index) const {
  const auto it = tiles_.find(index);
  return it == tiles_.end() ? nullptr : &it->second;
}

// ── WaterStageWorld ─────────────────────────────────────────────────────

void CliWorld::ResetWorkOwners() {
  for (ra2::rmg::PaintWorkCell& w : work_) {
    w.blob_id = -1;
    w.queued = -1;
  }
}

// ── RegionWorld 壳子 ────────────────────────────────────────────────────

void CliWorld::SetSubTileR(const ra2::rmg::CellCoord& c, int v) {
  SetSubTile(c, v);
}

// ── MakingWorld ─────────────────────────────────────────────────────────

int CliWorld::Ramp(const ra2::rmg::CellCoord& c) const { return At(c).ramp; }

int CliWorld::Overlay(const ra2::rmg::CellCoord& c) const {
  return At(c).overlay;
}

void CliWorld::SetOverlay(const ra2::rmg::CellCoord& c, int v) {
  Mut(c).overlay = static_cast<std::int16_t>(v);
}

int CliWorld::OverlayData(const ra2::rmg::CellCoord& c) const {
  return At(c).overlayData;
}

void CliWorld::SetOverlayData(const ra2::rmg::CellCoord& c, int v) {
  Mut(c).overlayData = static_cast<std::int8_t>(v);
}

void CliWorld::PlaceRepairHut(const ra2::rmg::CellCoord& c) {
  // 0x5904B0：盖一间 CABHUT——整条 RMG 流水线里唯一构造游戏对象的地方。
  BuildingRecord b;
  b.type = "CABHUT";
  b.owner = 0;
  b.x = c.x;
  b.y = c.y;
  b.health = 256;
  buildings_.push_back(b);
}

// ── HillWorld ───────────────────────────────────────────────────────────

int CliWorld::LevelH(const ra2::rmg::CellCoord& c) const { return Level(c); }

int CliWorld::RampH(const ra2::rmg::CellCoord& c) const { return Ramp(c); }

bool CliWorld::HasObject(const ra2::rmg::CellCoord& c) const {
  return At(c).object != 0;
}

bool CliWorld::TileMorphable(int tile) const {
  // 0x6B2520 那条 [0xA8ED38] 砖表长度 + TileTypes[tile]->+0x2E0 != 0。
  // tile 越界或 == 0xFFFF 都放行；砖号有效时看 nonmorph 集合。
  if (tile >= tile_count_ || tile == 0xFFFF) return true;
  return nonmorph_.count(tile) == 0;
}

int CliWorld::DummyLevel() const { return dummy_.level; }

int CliWorld::DummyRamp() const { return dummy_.ramp; }

void CliWorld::SetDummyLevel(int v) {
  dummy_.level = static_cast<std::int8_t>(v);
}

void CliWorld::SetDummyRamp(int v) {
  dummy_.ramp = static_cast<std::int8_t>(v);
}

// ── StartPointWorld ─────────────────────────────────────────────────────

int CliWorld::CellZoneId(const ra2::rmg::CellCoord& c) const {
  return At(c).zoneId;
}

int CliWorld::MovementZoneType(const ra2::rmg::CellCoord& c,
                               int movement_zone) const {
  // 0x56D230。区类型表由 pipeline 在 ComputeStartZoneSelection 后挂进来。
  if (zones_ == nullptr || zone_result_ == nullptr) return 0;
  return zones_->GetMovementZoneType(*zone_result_, c.x, c.y, movement_zone);
}

int CliWorld::IsoTileSP(const ra2::rmg::CellCoord& c) const {
  return IsoTile(c);
}

int CliWorld::LevelSP(const ra2::rmg::CellCoord& c) const { return Level(c); }

int CliWorld::RampSP(const ra2::rmg::CellCoord& c) const { return Ramp(c); }

ra2::rmg::CellCoord CliWorld::ReadBackCoord(const ra2::rmg::CellCoord& c) {
  const int s = SlotOf(c);
  if (s < 0 || exists_[static_cast<std::size_t>(s)] == 0) {
    // 0x5657C8：拿不到真实格子时把请求坐标写进哑元 +0x24 并回读。
    dummy_coord_ = c;
    return dummy_coord_;
  }
  return c;
}

void CliWorld::SetWaypoint(int index, const ra2::rmg::CellCoord& c) {
  // 0x594870 尾巴：写 ScenarioClass 第 index 号 waypoint。
  if (index >= static_cast<int>(waypoints_.size())) {
    waypoints_.resize(static_cast<std::size_t>(index) + 1);
  }
  waypoints_[static_cast<std::size_t>(index)] = c;
}

void CliWorld::ResetStartLocationTable() { start_locations_.clear(); }

void CliWorld::SetStartLocation(int slot, const ra2::rmg::CellCoord& c) {
  StartLocationRecord r;
  r.slot = slot;
  r.x = c.x;
  r.y = c.y;
  start_locations_.push_back(r);
}

void CliWorld::PlaceRandomBuilding(ra2::rmg::RegionClass* r) {
  // 0x595400。两个取数点：0x595461 抽种类（NeutralTechBuildings 表）、
  // 0x5954DA 每 attempt 抽一格区域格子。建筑种类只抽一次，位置最多 100 次。
  const int n = static_cast<int>(neutral_tech_buildings_.size());
  if (n <= 0) return;
  const int k = ra2::rmg::PickBelow(rng_, n);
  const std::string& type = neutral_tech_buildings_[static_cast<std::size_t>(k)];
  for (int attempt = 0; attempt < 100; ++attempt) {
    const int m = static_cast<int>(r->cells.size());
    if (m <= 0) return;
    const int j = ra2::rmg::PickBelow(rng_, m);
    const ra2::rmg::CellCoord at = r->cells[static_cast<std::size_t>(j)];
    // 逐格检查占地（默认 1×1，真实占地表 e2e 时校准）。
    if (HasOccupier(at)) continue;
    if (!IsPlain(at)) continue;
    if (Level(at) != r->level) continue;
    if (Field0xEC(at) == 3) continue;
    if (!WithinUsableArea(at)) continue;
    if (work_[WorkIdx(at)].flag45) continue;
    BuildingRecord b;
    b.type = type;
    b.owner = 0;
    b.x = at.x;
    b.y = at.y;
    b.health = 256;
    buildings_.push_back(b);
    Mut(at).object = 1;   // 占了这一格
    return;
  }
}

// ── TechWorld ───────────────────────────────────────────────────────────

int CliWorld::LevelT(const ra2::rmg::CellCoord& c) { return Level(c); }

bool CliWorld::IsPlain(const ra2::rmg::CellCoord& c) {
  // 0x486380：还没定砖（0 或 0xFFFF）
  return ra2::rmg::IsUndecidedTile(IsoTile(c));
}

bool CliWorld::HasOccupier(const ra2::rmg::CellCoord& c) {
  return At(c).object != 0;
}

int CliWorld::Field0xEC(const ra2::rmg::CellCoord& c) { return At(c).ground; }

bool CliWorld::WithinUsableArea(const ra2::rmg::CellCoord& c) {
  // 0x578540(cell, 1)：use_height = true
  return ra2::rmg::IsWithinUsableArea(grid_.width(), local_, c.x, c.y, Level(c),
                                      Ramp(c), true);
}

int CliWorld::NeutralTechBuildingCount() {
  return static_cast<int>(neutral_tech_buildings_.size());
}

bool CliWorld::AllocBuildingMemory() { return true; }

int CliWorld::CreateBuilding(int type_index) {
  // 不透明句柄：按创建顺序给号。type_index 是 NeutralTechBuildings 表下标。
  const int handle = static_cast<int>(active_buildings_.size());
  active_buildings_.push_back(type_index);
  return handle;
}

const std::vector<ra2::rmg::FoundationOffset>& CliWorld::Foundation(
    int handle) {
  // 单份占地表：默认 1×1（真实占地表从 rules 注入，e2e 时校准）。
  static const std::vector<ra2::rmg::FoundationOffset> kUnit = {
      {0, 0}, {0x7FFF, 0x7FFF}};
  (void)handle;
  return kUnit;
}

bool CliWorld::Unlimbo(int handle, const ra2::rmg::CellCoord& at) {
  if (handle < 0 || handle >= static_cast<int>(active_buildings_.size())) {
    return false;
  }
  const int type_index = active_buildings_[static_cast<std::size_t>(handle)];
  if (type_index < 0 ||
      type_index >= static_cast<int>(neutral_tech_buildings_.size())) {
    return false;
  }
  BuildingRecord b;
  b.type = neutral_tech_buildings_[static_cast<std::size_t>(type_index)];
  b.owner = 0;
  b.x = at.x;
  b.y = at.y;
  b.health = 256;
  buildings_.push_back(b);
  return true;
}

void CliWorld::Destroy(int handle) {
  if (handle < 0 || handle >= static_cast<int>(active_buildings_.size())) {
    return;
  }
  active_buildings_[static_cast<std::size_t>(handle)] = -1;
}

// ── OreWorld（非 const 重载） ───────────────────────────────────────────

int CliWorld::IsoTile(const ra2::rmg::CellCoord& c) {
  return static_cast<const CliWorld*>(this)->IsoTile(c);
}

int CliWorld::Level(const ra2::rmg::CellCoord& c) {
  return static_cast<const CliWorld*>(this)->Level(c);
}

int CliWorld::Ramp(const ra2::rmg::CellCoord& c) {
  return static_cast<const CliWorld*>(this)->Ramp(c);
}

int CliWorld::Overlay(const ra2::rmg::CellCoord& c) {
  return static_cast<const CliWorld*>(this)->Overlay(c);
}

int CliWorld::OverlayData(const ra2::rmg::CellCoord& c) {
  return static_cast<const CliWorld*>(this)->OverlayData(c);
}

int CliWorld::ContainedTiberiumIndex(const ra2::rmg::CellCoord& c) {
  // 只用到 != -1。矿的 overlay 序号见 CreateOreField（102/27 变体），
  // 这里按「有 overlay 就报有矿」的保守口径，e2e 时校准。
  return Overlay(c) == -1 ? -1 : 0;
}

void CliWorld::CreateTerrain(const char* name,
                             const ra2::rmg::CellCoord& at) {
  TerrainRecord t;
  t.name = name;
  t.x = at.x;
  t.y = at.y;
  terrains_.push_back(t);
}

ra2::rmg::CellCoord CliWorld::Waypoint(int index) {
  if (index < 0 || index >= static_cast<int>(waypoints_.size())) {
    return ra2::rmg::CellCoord{0, 0};
  }
  return waypoints_[static_cast<std::size_t>(index)];
}

// ── LatWorld（非 const 重载 / 谓词） ────────────────────────────────────

void CliWorld::SetIsoTileLat(const ra2::rmg::CellCoord& c, int v) {
  SetIsoTile(c, v);
}

void CliWorld::SetOverlayLat(const ra2::rmg::CellCoord& c, int v) {
  SetOverlay(c, v);
}

int CliWorld::Field0xE4(const ra2::rmg::CellCoord& c) { return At(c).object; }

bool CliWorld::IsPaveTile(const ra2::rmg::CellCoord& c) {
  return ra2::rmg::IsPaveTile(IsoTile(c), theater_.pave_tile);
}

bool CliWorld::IsMiscPaveTile(const ra2::rmg::CellCoord& c) {
  return ra2::rmg::IsMiscPaveTile(IsoTile(c), theater_.misc_pave_tile);
}

bool CliWorld::IsShorePiece(const ra2::rmg::CellCoord& c) {
  // 0x4865B0
  return ra2::rmg::IsShoreTile(IsoTile(c), theater_.shore_pieces);
}

bool CliWorld::IsClearToSandLat(const ra2::rmg::CellCoord& c) {
  // 0x486790
  const int tile = IsoTile(c);
  return tile >= theater_.clear_to_sand_lat &&
         tile < theater_.clear_to_sand_lat + 0x10;
}

bool CliWorld::IsGreen(const ra2::rmg::CellCoord& c) {
  // 0x4867B0
  return ra2::rmg::IsGreenTile(IsoTile(c), theater_);
}

void CliWorld::SetupLAT(const ra2::rmg::CellCoord& c) {
  // 0x47CA80：把涂出来的 LAT 块按邻接关系换成正确的过渡片。无随机数。
  // 汇编 0x47CB9B-0x47D1CF 是九组线性序列：四段过渡（Rough/Sand/Green/Pave）
  // + 一段 Ramp。组宽、"同组"判据、入口 -1 检查全部照抄汇编；方向表
  // 0x89F688 在 .bss（文件里没有），取值从运行中的游戏进程取证。
  CliCell& cell = Mut(c);

  const auto in = [](int v, int a, int b) { return v >= a && v <= b; };
  static const std::int8_t kDir[8][2] = {
      {0, -1}, {1, -1}, {1, 0}, {1, 1},
      {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
  const auto step = [&](const ra2::rmg::CellCoord& p, int d) {
    return ra2::rmg::CellCoord{p.x + kDir[d][0], p.y + kDir[d][1]};
  };

  // 段 1：ClearToRoughLat（0xAA1134，宽 0xF）。入口 = 砖==RoughTile 或组内，
  // 无 -1 检查（汇编 0x47CB9B-0x47CC46 原样）。
  {
    const int start = theater_.clear_to_rough_lat;
    const int rough = theater_.rough_tile;
    const int tile = cell.isoTile;
    if (tile == rough || in(tile, start, start + 0xF)) {
      int m = 0;
      for (int i = 0; i < 4; ++i) {
        const int nt = IsoTile(step(c, i * 2));
        if (nt == rough || in(nt, start, start + 0xF)) continue;
        m |= 1 << i;
      }
      cell.isoTile = static_cast<std::int16_t>(m ? start + m : rough);
    }
  }

  // 段 2：ClearToSandLat（0xAA0E24，宽 0xF）。入口要 -1 检查。
  {
    const int start = theater_.clear_to_sand_lat;
    const int sand = theater_.sand_tile;
    if (start != -1) {
      const int tile = cell.isoTile;
      if (tile == sand || in(tile, start, start + 0xF)) {
        int m = 0;
        for (int i = 0; i < 4; ++i) {
          const int nt = IsoTile(step(c, i * 2));
          if (nt == sand || in(nt, start, start + 0xF)) continue;
          m |= 1 << i;
        }
        cell.isoTile = static_cast<std::int16_t>(m ? start + m : sand);
      }
    }
  }

  // 段 3：ClearToGreenLat（0xAA0748，宽 0xF）。邻居在 ShorePieces（宽 0x29）
  // 或 WaterBridge（宽 1）组内也算同组。
  {
    const int start = theater_.clear_to_green_lat;
    const int green = theater_.green_tile;
    if (start != -1) {
      const int tile = cell.isoTile;
      if (tile == green || in(tile, start, start + 0xF)) {
        const int shore0 = theater_.shore_pieces;
        const int shore1 = shore0 + 0x29;
        const int wb0 = theater_.water_bridge;
        const int wb1 = wb0 + 1;
        int m = 0;
        for (int i = 0; i < 4; ++i) {
          const int nt = IsoTile(step(c, i * 2));
          if (nt == green || in(nt, start, start + 0xF)) continue;
          if (in(nt, shore0, shore1) || in(nt, wb0, wb1)) continue;
          m |= 1 << i;
        }
        cell.isoTile = static_cast<std::int16_t>(m ? start + m : green);
      }
    }
  }

  // 段 4：ClearToPaveLat（0xAA10A8，宽 0xF）。邻居在 MiscPaveTile（宽 0xD）、
  // Medians（宽 0xD）、PavedRoads（宽 0x14）组内也算同组。
  {
    const int start = theater_.clear_to_pave_lat;
    const int pave = theater_.pave_tile;
    if (start != -1) {
      const int tile = cell.isoTile;
      if (tile == pave || in(tile, start, start + 0xF)) {
        const int mp0 = theater_.misc_pave_tile;
        const int mp1 = mp0 + 0xD;
        const int md0 = theater_.medians;
        const int md1 = md0 + 0xD;
        const int pr0 = theater_.paved_roads;
        const int pr1 = pr0 + 0x14;
        int m = 0;
        for (int i = 0; i < 4; ++i) {
          const int nt = IsoTile(step(c, i * 2));
          if (nt == pave || in(nt, start, start + 0xF)) continue;
          if (in(nt, mp0, mp1) || in(nt, md0, md1) || in(nt, pr0, pr1)) {
            continue;
          }
          m |= 1 << i;
        }
        cell.isoTile = static_cast<std::int16_t>(m ? start + m : pave);
      }
    }
  }

  // 段 5：Ramp。入口 = 砖∈[RampBase, +0x13] 或 [RampSmooth, +0xB]。
  // 按 ramp(+0x11C) 分派两方向邻居（邻居 ramp==0 才设位），过渡公式：
  //   ramp1: +edi-1  ramp2: +edi+2  ramp3: +edi+5  ramp4: +edi+8
  // 兜底（无过渡位）= RampBase + ramp - 1。
  {
    const int rbase = theater_.ramp_base;
    const int rsmooth = theater_.ramp_smooth;
    const int tile = cell.isoTile;
    if (in(tile, rbase, rbase + 0x13) || in(tile, rsmooth, rsmooth + 0xB)) {
      const int ramp = cell.ramp;
      int edi = 0;
      int d1 = -1, d2 = -1, bias = 0;
      switch (ramp) {
        case 1: d1 = 6; d2 = 2; bias = -1; break;
        case 2: d1 = 0; d2 = 4; bias = 2; break;
        case 3: d1 = 2; d2 = 6; bias = 5; break;
        case 4: d1 = 4; d2 = 0; bias = 8; break;
        default: break;
      }
      if (d1 >= 0) {
        if (At(step(c, d1)).ramp == 0) edi |= 1;
        if (At(step(c, d2)).ramp == 0) edi |= 2;
        if (edi != 0) {
          cell.isoTile = static_cast<std::int16_t>(rsmooth + edi + bias);
        }
      }
      if (edi == 0) {
        cell.isoTile = static_cast<std::int16_t>(rbase + ramp - 1);
      }
    }
  }
}

void CliWorld::RecalcAttributes(const ra2::rmg::CellCoord& c, int arg) {
  // 0x47D2B0 的随机地图子集：地形三件套（recalc.cpp 已实现）+ 无条件重算
  // +0xEC（地面类型）与 +0x4C（地面类别），并把两者喂给格子。
  ra2::rmg::RecalcAttributes(*this, c, tile_count_);

  // 0x47D7FB-0x47D813：subtile 有效的格子重算完坡道后紧接 SetupLAT
  // （0x47CA80，贴过渡片）。被清格的（0xFFFF）不走这条路。
  if (Mut(c).isoTile != 0xFFFF) {
    SetupLAT(c);
  }

  CliCell& cell = Mut(c);
  // 0x47D54A 之前那段 +0xEC 的重算：从砖表子块头 +0x29 查 TerrainToGround。
  const int tile = cell.isoTile;
  if (tile == 0xFFFF || tile >= tile_count_) {
    cell.ground = 0;
  } else {
    const ra2::rmg::TileType* tt = TileByIndex(tile);
    const int subtile = cell.subtile;
    int ground = 0;
    if (tt != nullptr && subtile >= 0 &&
        static_cast<std::size_t>(subtile) < tt->cells.size()) {
      const int terrain = tt->cells[static_cast<std::size_t>(subtile)].terrain_type;
      if (terrain >= 0 && terrain < 16) {
        ground = ra2::rmg::kTerrainToGround[terrain];
      }
    }
    cell.ground = static_cast<std::int16_t>(ground);
  }
  // 0x483C80：地面类别。出图判定用 IsWithinUsableArea(use_height=true)。
  const bool within = ra2::rmg::IsWithinUsableArea(
      grid_.width(), local_, c.x, c.y, cell.level, cell.ramp, true);
  if (ground_foot_.empty()) {
    cell.zoneId = within ? 0 : 7;
  } else {
    cell.zoneId = ra2::rmg::RecalcLandType(
        within, cell.ground, ground_foot_.data());
  }
}

// ── 便捷访问 ────────────────────────────────────────────────────────────

}  // namespace cli
}  // namespace ra2
