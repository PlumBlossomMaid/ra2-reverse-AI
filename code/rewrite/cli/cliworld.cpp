// CliWorld 实现——见 cliworld.h 头部注释。
//
// 一份格子状态 + 一份工作数组，同时喂给流水线全部阶段。菱形外的格子统一退到
// 共享哑元 dummy_（原版 0xABDC50 语义），上一次留在哑元里的字段会被下一次读到。
#include "cliworld.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

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

void CliWorld::InitCells(bool first_run) {
  // 首局 = 原版 0x599650 重置子路径（0x599E70-0x599EB7 的 CellIterator 清
  // 6 字段）；第 2 局起该路径被跳过，cells 全字段原样残留。
  if (first_run) {
    grid_.ForEachCell([&](const ra2::rmg::CellCoord& c) {
      CliCell& cell = Mut(c);
      cell.ramp = 0;         // +0x11C
      cell.level = 4;        // +0x11B = 常数 4
      cell.isoTile = 0;      // +0x38
      cell.subtile = 0;      // +0x11A
      cell.overlay = -1;     // +0x44 = 0xFFFFFFFF
      cell.overlayData = 0;  // +0x11E
      // 原版阶段 1 只重置上面 6 个字段，不碰
      // +0xE4(object)/+0xEC(ground)/+0x4C(zoneId)。
    });
  }
  // 工作数组（stage-init.md §五 / 0x59A321-0x59A376 的初始化循环）：
  // **每局** free+new 后全零，但 +0x42 = 1（有效位）、+0x38 = -1
  // （掩码缓存未计算）。
  for (ra2::rmg::PaintWorkCell& w : work_) {
    w = ra2::rmg::PaintWorkCell{};
    w.flag4a = true;
    w.mask_cache = -1;
  }
  // 尾循环（0x59A385-0x59A3DC，每局都跑）：打包坐标写进记录头 4 字节，
  // cell +0x11B 改成 MapSeed +0x30C（= 4）。
  grid_.ForEachCell([&](const ra2::rmg::CellCoord& c) {
    work_[WorkIdx(c)].coord = c;
    Mut(c).level = 4;
  });
  buildings_.clear();
  start_locations_.clear();
  waypoints_.clear();
  active_buildings_.clear();
}

bool CliWorld::LoadInitCells12(const std::string& path, std::string* err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (err) *err = "cannot open init-cells: " + path;
    return false;
  }
  char rec[12];
  for (int i = 0; i < kCellSlots; ++i) {
    if (!f.read(rec, 12)) {
      if (err) *err = "init-cells truncated (need 12B x 0x40000): " + path;
      return false;
    }
    if (rec[0] == 0) continue;  // 不存在的槽保持不存在
    if (exists_[static_cast<std::size_t>(i)] == 0) continue;
    CliCell& c = cells_[static_cast<std::size_t>(i)];
    std::int16_t iso = 0;
    std::int16_t ov = 0;
    std::memcpy(&iso, rec + 1, 2);  // int16 LE，有符号装载
    std::memcpy(&ov, rec + 7, 2);   // -1 = 无 overlay
    c.isoTile = iso;
    c.subtile = static_cast<std::int8_t>(rec[3]);
    c.level = static_cast<std::int8_t>(rec[4]);
    c.ramp = static_cast<std::int8_t>(rec[5]);
    c.overlayData = static_cast<std::int8_t>(rec[6]);
    c.overlay = ov;
    c.object = (rec[9] != 0) ? 1 : 0;  // 占用布尔（DLL 端也只 dump 布尔）
    c.ground = static_cast<std::uint8_t>(rec[10]);
    c.zoneId = static_cast<std::uint8_t>(rec[11]);
  }
  return true;
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

void CliWorld::SetExist(int x, int y, bool v) {
  const int s = ((y & 0x1FF) << 9) | (x & 0x1FF);
  exists_[static_cast<std::size_t>(s)] = v ? 1 : 0;
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
  // 无效格退到共享哑元，原版阶段 10 的 SetupLAT 需要读取其残留砖号。
  if (s < 0 || exists_[static_cast<std::size_t>(s)] == 0) {
    return static_cast<std::int32_t>(
        static_cast<std::uint16_t>(dummy_.isoTile));
  }
  return static_cast<std::int32_t>(
      static_cast<std::uint16_t>(cells_[static_cast<std::size_t>(s)].isoTile));
}

void CliWorld::SetIsoTile(const ra2::rmg::CellCoord& c, int v) {
  Mut(c).isoTile = static_cast<std::int16_t>(v);
  if (v != 0) loaded_tiles_.insert(v);  // 模拟原版 .tmp lazy load：
                                         // PlaceTile 贴非零砖时加载 .tmp（+0xA4）；
                                         // tile 0 (ClearTile) 不经 PlaceTile，不加载
}

void CliWorld::SetCellSubTile(const ra2::rmg::CellCoord& c, int v) {
  Mut(c).subtile = static_cast<std::int8_t>(v);
}

void CliWorld::SetCellLevel(const ra2::rmg::CellCoord& c, int v) {
  Mut(c).level = static_cast<std::int8_t>(v);
}

bool CliWorld::SmoothShoreline(int blob_id) {
  // 原版两个调用点共用一个虚函数槽：涂块收尾 0x59C433 传 1，
  // 湖/瀑布收尾 0x59D282 传 0。flag 为真时贴砖器关工作数组读写、
  // 归属恒判「同块」；为假时岸线第 1 遍侵蚀会撞已有陆地块 → 整块回滚
  // （mt1 第 2 块起全灭的根因）。湖路径由 pipeline 提前 SetShorePaintFlag(false)。
  return ra2::rmg::ShorelineSmooth(*this, grid_, theater_, rng_, blob_id,
                                   shore_paint_flag_);
}

// ── ShoreWorld ──────────────────────────────────────────────────────────

bool CliWorld::WorkValid(const ra2::rmg::CellCoord& c) const {
  return work_[WorkIdx(c)].flag4a;
}

int CliWorld::MaskCache(const ra2::rmg::CellCoord& c) const {
  return work_[WorkIdx(c)].mask_cache;
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
  work_[WorkIdx(c)].mask_cache = v;
}

void CliWorld::ResetMaskCaches() {
  for (ra2::rmg::PaintWorkCell& w : work_) w.mask_cache = -1;
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
  // 0x595400。两个取数点都是**两次乘法**那一路（PickBelowMul）：
  // 0x595461 抽种类（NeutralTechBuildings 表）、0x5954DA 每 attempt 抽一格
  // 区域格子。建筑种类只抽一次，位置最多 100 次；占地表是该建筑类型
  // 虚函数 +0x90(1) 返回的 (dx,dy) 列表（锚点 = 左上角），逐格检查、
  // 第一格失败即放弃本次 attempt；层高比的是**锚点格**（不是区域 level）。
  const int n = static_cast<int>(neutral_tech_buildings_.size());
  if (n <= 0) return;
  const int k = ra2::rmg::PickBelowMul(rng_, n);
  const int handle = CreateBuilding(k);
  bool placed = false;                                  // [esp+0x13]
  for (int attempt = 0; attempt < 100 && !placed; ++attempt) {
    const int m = static_cast<int>(r->cells.size());
    if (m <= 0) break;   // 原版此时越界读 cells[0]，真·未定义，实际不发生
    const int j = ra2::rmg::PickBelowMul(rng_, m);
    const ra2::rmg::CellCoord at = r->cells[static_cast<std::size_t>(j)];
    const int base_level = Level(at);                   // 0x595522 锚点 +0x11B
    const std::vector<ra2::rmg::FoundationOffset>& fnd = Foundation(handle);
    bool ok = true;                                     // bl
    for (std::size_t i = 0;; ++i) {
      const ra2::rmg::FoundationOffset& f = fnd[i];
      if (f.dx == 0x7FFF && f.dy == 0x7FFF) {           // 0x59553B 表尾
        if (ok) placed = Unlimbo(handle, at);           // 0x5955F0 虚函数 +0xD8
        break;
      }
      // 0x59554E 起：占地偏移与锚点按 **int16** 相加
      const ra2::rmg::CellCoord nc{
          static_cast<std::int16_t>(f.dx + at.x),
          static_cast<std::int16_t>(f.dy + at.y)};
      // +0x45 的工作记录下标由**请求坐标**算（出图时 GetCellAt 落哑元，
      // 哑元 +0x24/+0x26 刚被写成请求坐标，所以下标与 nc 一致）
      if (HasOccupier(nc) ||                            // 0x595580 +0xE4
          !IsPlain(nc) ||                               // 0x59558C 0x486380
          Level(nc) != base_level ||                    // 0x595595
          Field0xEC(nc) == 3 ||                         // 0x5955A0
          !WithinUsableArea(nc) ||                      // 0x5955B1 0x578540(c,1)
          work_[static_cast<std::size_t>(nc.y * grid_.side() + nc.x)].flag45) {
        ok = false;                                     // 0x5955DE
      }
      if (!ok) break;                                   // 第一格失败即弃
    }
  }
  if (!placed) Destroy(handle);                         // 0x595655 虚函数 +0x20(1)
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
  static const std::vector<ra2::rmg::FoundationOffset> kUnit = {
      {0, 0}, {0x7FFF, 0x7FFF}};
  foundation_ = kUnit;
  if (handle >= 0 && handle < static_cast<int>(active_buildings_.size())) {
    const int type_index = active_buildings_[static_cast<std::size_t>(handle)];
    if (type_index >= 0 &&
        type_index < static_cast<int>(neutral_tech_buildings_.size())) {
      const std::string& type =
          neutral_tech_buildings_[static_cast<std::size_t>(type_index)];
      const auto it = foundations_.find(type);
      if (it != foundations_.end()) foundation_ = it->second;
    }
  }
  return foundation_;
}

bool CliWorld::Unlimbo(int handle, const ra2::rmg::CellCoord& at) {
  if (!tech_object_registration_) {
    std::fprintf(stderr, "[DEBUG Unlimbo] tech_object_registration_ is false\n");
    return false;
  }
  if (handle < 0 || handle >= static_cast<int>(active_buildings_.size())) {
    std::fprintf(stderr, "[DEBUG Unlimbo] Invalid handle: %d (size=%zu)\n",
                 handle, active_buildings_.size());
    return false;
  }
  const int type_index = active_buildings_[static_cast<std::size_t>(handle)];
  if (type_index < 0 ||
      type_index >= static_cast<int>(neutral_tech_buildings_.size())) {
    std::fprintf(stderr, "[DEBUG Unlimbo] Invalid type_index: %d (size=%zu)\n",
                 type_index, neutral_tech_buildings_.size());
    return false;
  }
  const std::string& type =
      neutral_tech_buildings_[static_cast<std::size_t>(type_index)];
  BuildingRecord b;
  b.type = type;
  b.owner = 0;
  b.x = at.x;
  b.y = at.y;
  b.health = 256;
  buildings_.push_back(b);
  for (std::size_t i = 0; i + 1 < foundation_.size(); ++i) {
    const ra2::rmg::CellCoord n{
        static_cast<std::int16_t>(at.x + foundation_[i].dx),
        static_cast<std::int16_t>(at.y + foundation_[i].dy)};
    if (CellExists(n)) Mut(n).object = 1;
  }
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
  // 0x5FDD20 语义：ecx(overlay)==-1 → 返回 -1；非 -1 时查 OverlayTypeClass 表
  // （[0xA83D84] + ov*4），+0x2A9 tiberium 标志为真则继续，命中返回非 -1，
  // 否则返回 -1。
  // ⚠️ 区间来自 CreateOreField 的 base（0x5A2953 `(-(uint)(is_gems) &
  // 0xffffffb5) + 0x66` → 27/102）+ v=PickBelow(12) ∈ [0,11]，即 gems 写
  // ov 27..38、tib 写 ov 102..113。这与 rulesmd.ini（YR mod）的
  // [OverlayTypes] 排序**不一致**（mod 里 27=NAWALL、102-104=LOBRDG26-28），
  // 但原版 YR 1.00 的 rules.ini 里 27-38=GEM01-12、102-113=TIB01-12，
  // 与 base 吻合。权威快照 ov=102-104 有 41 格 od∈{1,3,4,6,7,8,10,11}
  // 非零（走条件 B 加厚）——铁证：102-104 在本版确为矿石（Tiberium=yes）。
  // 若把区间改成 mod 排序（28-39/105-116），会把原版矿石格误判为非矿，
  // 条件 B 永不触发 → 整张图无 od 加厚。
  const int ov = Overlay(c);
  if ((ov >= 102 && ov <= 113) || (ov >= 27 && ov <= 38)) return 0;
  return -1;
}

void CliWorld::CreateTerrain(const char* name,
                             const ra2::rmg::CellCoord& at) {
  TerrainRecord t;
  t.name = name;
  t.x = at.x;
  t.y = at.y;
  terrains_.push_back(t);
  terrain_by_cell_[at.Packed()] = name;  // RecalcLandType 占据者循环用
  Mut(at).object = 1;   // 原版 0x71BB90 置 cell +0xE4，供 GrowTreePatch 种植检查
  // 0x71D000（TerrainClass 放置，ctor 内调）：cell 有 overlay 且其
  // OverlayTypeClass +0x2A9（INI Tiberium=yes）为真 → overlay=-1、
  // overlayData=0。矿田里锚定 TIBTRE 会清掉该格已铺的矿再重铺，
  // 缺了这一步整片矿扩散路径全错位（mt0 布矿发散根因，2026-10-08 活体实锤）。
  CliCell& c = Mut(at);
  if (c.overlay != -1 && OverlayLandOf(c.overlay).tiberium) {
    c.overlay = -1;
    c.overlayData = 0;
  }
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

  // 段 3：ClearToGreenLat（0xAA0748，宽 0xF）。邻居豁免（0x47CD9B-0x47CDC1）：
  //   1. green_tile 或 [clear_to_green_lat, +0xF] → 同组
  //   2. [shore_pieces, +0x29] → shore 豉免
  //   3. [WaterBridge(0xAA1090), +1] → WaterBridge
  // ⚠️ 2026-10-05：原版 0xAA1090 读的是 WaterBridge=804，不是 ClearTile=0。
  //   theater.h 字段注释曾把 0xAA1090 标成 clear_tile，豁免区间错成 [0,1]。
  //   修正：改用 theater_.water_bridge（0xAA1090 的真值 804）。
  {
    const int start = theater_.clear_to_green_lat;
    const int green = theater_.green_tile;
    if (start != -1) {
      const int tile = cell.isoTile;
      if (tile == green || in(tile, start, start + 0xF)) {
        const int shore0 = theater_.shore_pieces;
        const int shore1 = shore0 + 0x29;
        const int wb0 = theater_.water_bridge;  // 0xAA1090，非 ClearTile
        const int wb1 = wb0 + 1;
        int m = 0;
        for (int i = 0; i < 4; ++i) {
          const int nt = IsoTile(step(c, i * 2));
          if (nt == green || in(nt, start, start + 0xF)) continue;
          if (in(nt, shore0, shore1)) continue;
          if (wb0 != -1 && in(nt, wb0, wb1)) continue;
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
  // ⚠️ 2026-10-04：无全局 fallback。原版 0x47D1BC-0x47D1CC 的 fallback
  // 只在 Ramp 段内部（tile ∈ ramp 组但无方向匹配），写 ramp_base + ramp - 1。
  // 不匹配任何 LAT 组的格子保持原值不变。
}

void CliWorld::RecalcAttributes(const ra2::rmg::CellCoord& c, int arg) {
  // 临时探针（RA2_PROBE_CELL=x,y 门控）：观测失败格的完整轨迹
  static int probe_x = -2, probe_y = -2;
  static bool probe_init = false;
  if (!probe_init) {
    probe_init = true;
    const char* e = std::getenv("RA2_PROBE_CELL");
    if (e) std::sscanf(e, "%d,%d", &probe_x, &probe_y);
  }
  const bool probe = (c.x == probe_x && c.y == probe_y);
  if (probe) {
    const CliCell& cc = At(c);
    std::fprintf(stderr,
        "[PROBE] enter (%d,%d) iso=%d sub=%d lv=%d ramp=%d ov=%d od=%d gnd=%d zone=%d cbi=%d\n",
        c.x, c.y, IsoTile(c), (int)cc.subtile, (int)cc.level, (int)cc.ramp,
        Overlay(c), (int)cc.overlayData, (int)cc.ground, (int)cc.zoneId,
        cliff_back_impassability_);
  }
  // 0x47D2F6-0x47D32D：overlay != -1 时**无条件**写
  // +0xEC = OverlayTypeClass+0x298（任何 overlay，不止矿石——活体石头格
  // iso=423/493 仍 ground=3 实锤；CLI 旧版只对矿写、其余走 tile 查表是错的）。
  // +0x298∈{4,9} 或 overlay 占地（原版 +0x2AC；INI 口径 Tiberium ||
  // NoUseTileLandType）→ overlay 专属路径 0x47D333：不经
  // SubTilePresent/SetupLAT/tile 查表/空洞清理，CliffBack 变体
  // （0x47D386-0x47D546，6 方向与空洞路径第一段同款）命中且
  // Rules+0x664==2 时**无条件** ground=3，随后 0x47CA80 +
  // RecalcLandType（0x47D551）——矿格也照走（CLI 旧版 ground==5 早退
  // 跳过 RecalcLandType 是错的：活体 TIBTRE-on-ore 格 zone=2 就是
  // 0x483C80 的占据者循环算出来的）。
  {
    CliCell& ocell = Mut(c);
    if (ocell.overlay != -1) {
      const ra2::rmg::OverlayLandInfo ol = OverlayLandOf(ocell.overlay);
      ocell.ground = static_cast<std::int16_t>(ol.land);
      if (ol.owns_land) {
        // 0x47D364：ramp != 0 && OverlayType+0x2A9 → overlay=-1/od=0。
        // 活体 dump：RMG 全部 overlay 格 ramp=0/sub=0，分支不触发，不建模。
        if (cliff_back_impassability_ != 0) {
          const int cell_lv = ocell.level;
          bool cliff_back = false;
          const int kDir[6][2] = {{0, -1}, {-1, 0}, {2, 2},
                                  {1, 1},  {-1, 1}, {1, -1}};
          for (const auto& d : kDir) {
            const ra2::rmg::CellCoord n{
                static_cast<std::int16_t>(c.x + d[0]),
                static_cast<std::int16_t>(c.y + d[1])};
            if (!CellExists(n)) continue;
            if (At(n).level >= cell_lv + 4) { cliff_back = true; break; }
          }
          if (cliff_back && cliff_back_impassability_ == 2) {
            ocell.ground = 3;  // 0x47D53E：无条件写（不看 ground 值）
          }
        }
        ocell.zoneId = static_cast<std::uint8_t>(ComputeLandType(c));
        return;
      }
      // land∉{4,9} 且不占地 → 0x47D57B tile 路径（ground 已被写为 land，
      // 随后 tile 查表/空洞清理覆盖；RMG 无此情形）。
    }
  }
  // 0x47D2B0 的随机地图子集：地形三件套（recalc.cpp 已实现）+ 无条件重算
  // +0xEC（地面类型）与 +0x4C（地面类别），并把两者喂给格子。
  // ⚠️ 只在这里做地形三件套（rmg 版不含 overlay/ground 语义）。
  ra2::rmg::RecalcAttributes(*this, c, tile_count_);

  // 0x47D7FB-0x47D813：subtile 有效的格子重算完坡道后紧接 SetupLAT
  // （0x47CA80，贴过渡片）。被清格的（0xFFFF）不走这条路。
  // ⚠️ 原版 SubTilePresent 检查 TileTypeClass+0xA4（.tmp 映像是否已加载）。
  // .tmp 是 lazy load——首次通过 PlaceTile 使用时才加载。缺文件的砖
  // （TileType.loaded==false）永远加载不上 → SubTilePresent 返回 false
  // → 走空洞路径。
  const int tile = IsoTile(c);
  const ra2::rmg::TileType* tt = (tile >= 0 && tile < tile_count_)
                                     ? TileByIndex(tile)
                                     : nullptr;
  // .tmp 映像在原版进程里启动时**全部预加载**（live 取证：838 块砖的
  // TileTypeClass+0xA4 全非空，包括 tile 0）。CLI 早期用 loaded_tiles_ 模拟
  // lazy load，但实测会漏判 LAT 过渡砖（SetupLAT 直接写 isoTile，不经
  // SetIsoTile，导致 +0xA4 未登记 → 误走空洞路径 ground=0）。改为判断
  // .tmp 是否解析出子块表——cells 非空即对应原版「映像指针非空」。
  const bool tmp_present = tt != nullptr && !tt->cells.empty();
  const bool empty_path =
      (tile == 0xFFFF) || (tile >= tile_count_) || !tmp_present;
  if (probe)
    std::fprintf(stderr,
        "[PROBE] branch tile=%d tile_count=%d tt=%p cells=%d tmp=%d empty=%d\n",
        tile, tile_count_, (const void*)tt, tt ? (int)tt->cells.size() : -1,
        (int)tmp_present, (int)empty_path);
  if (!empty_path) SetupLAT(c);

  CliCell& cell = Mut(c);
  // 0x47D54A 之前那段 +0xEC 的重算：从砖表子块头 +0x29 查 TerrainToGround。
  // 有效路径（SubTilePresent=true，即 .tmp 已加载）走 0x544BE0；空洞路径 ground=0。
  // （矿格已在函数开头的 overlay 专属路径处理——Tiberium 属性恒占地，
  // 不再到达这里，旧的 ContainedTiberiumIndex 特判随之移除。）
  if (empty_path) {
    // ⚠️ 空路径（原版 0x47D5E6-0x47D5F9）：SubTilePresent=false 时
    // 清 tile=0xFFFF / ground=0 / ramp=0，再走 CliffBack 第一段。
    cell.ground = 0;
    // 0x47D5FF-0x47D7C1：CliffBackImpassability。**只在空洞路径里执行**
    //（原版 SubTilePresent=false 才进这一段；有效路径查完表直接去
    // 0x47D7CB RecalcLandType，不经过这里）。若 Rules 字段 != 0 且存在
    // 邻居 level >= cell_level + 4（cell 在悬崖脚下，邻居比它高 ≥4），
    // 且字段 == 2 且 ground == 0（Clear），则强制 ground = 3（Rock）。
    // 原版 8 邻居循环 0x47D613-0x47D7A9 的反汇编（0x47D63D-0x47D650）：
    //   movsx edx, [eax+0x11B]   ; edx = 邻居 level
    //   movsx ecx, [esi+0x11B]   ; ecx = cell level
    //   add edx, 4
    //   cmp edx, ecx
    //   jle  0x47D7A9           ; 邻居+4 <= cell → **跳过** CliffBack
    //   jg 分支 → cliff_back=true（邻居+4 > cell，即邻居 >= cell+4 或
    //   邻居 == cell 时 +4 > cell，等价 cell+4 <= 邻居 → 悬崖脚下）
    // 权威实测（cells_at_rebuild 复算）：iso==0 且 ground=3 的 301 格里
    // 168 格满足 `cell+4 <= 邻居`、0 格满足 `邻居+4 <= cell`——方向实锤。
    // ⚠️ 空洞路径里 ground 恒为 0，所以 `cell.ground == 0` 恒真。
    // 动态实测（0x594BAE, pid=24532, 2026-10-03）：(50,46) 的邻居
    // SE (52,48) lvl=8 满足 cell+4<=neighbor（CliffBack 会写 3）。
    // 而 W (49,46) lvl=4 / SW (49,47) lvl=4 被洞穿后 ground=0 是
    // **原版行为**（原版也是 ground=0），不是 CLI 缺陷——两条走向
    // 都是正确的。
    // ⚠️⚠️ 邻居方向是**6 个等距对角**，不是 8 邻域（0x47D613-0x47D7A9）：
    //   N (x,y-1) / W (x-1,y) / SE (x+2,y+2) / E (x+1,y+1) /
    //   SW (x-1,y+1) / NE (x+1,y-1)
    // SE 是 (+2,+2) 而非 (+1,+1)——用 8 邻域 (dx,dy∈{-1,0,1}) 会**漏掉**
    // SE 方向的高邻居（2026-10-03 实锤：125 格 n6_0 漏判全部因此，
    // 28 格 n0_6 多判则因 8 邻域多查了 6 方向没有的 (0,+1)/(+1,0) 等）。
    if (cliff_back_impassability_ != 0) {
      const int cell_lv = cell.level;
      bool cliff_back = false;
      const int kDir[6][2] = {{0, -1}, {-1, 0}, {2, 2},
                              {1, 1},  {-1, 1}, {1, -1}};
      for (const auto& d : kDir) {
        const ra2::rmg::CellCoord n{
            static_cast<std::int16_t>(c.x + d[0]),
            static_cast<std::int16_t>(c.y + d[1])};
        if (!CellExists(n)) continue;
        if (At(n).level >= cell_lv + 4) { cliff_back = true; break; }
      }
      if (cliff_back && cliff_back_impassability_ == 2) {
        // 空路径 0x47D7A9 先 `test [0xEC]`，ground==0 才写 3
        // （空路径 ground 恒 0，检查形同虚设）。overlay 占地格的
        // CliffBack 在函数开头的 overlay 专属路径处理（0x47D52F）。
        if (cell.ground == 0) {
          cell.ground = 3;
        }
      }
    }
  } else {
    const int subtile = cell.subtile;
    int ground = 0;
    if (subtile >= 0 &&
        static_cast<std::size_t>(subtile) < tt->cells.size()) {
      const int terrain = tt->cells[static_cast<std::size_t>(subtile)].terrain_type;
      if (terrain >= 0 && terrain < 16) {
        ground = ra2::rmg::kTerrainToGround[terrain];
      }
    }
    // 0x47D818-0x47D8B0：SetupLAT 之后、CliffBack 之前按 overlay 改写 +0xEC。
    //   overlay == -1                          → 保持 0x544BE0 的查表结果
    //   0x5FDD20(overlay) == -1（非矿）        → 同上（走到这里时必不占地）
    //   是矿 且 ramp(+0x11C) > 4               → ground=查表结果、overlay=-1、od=0
    //   是矿 且 ramp <= 4（0x47D859）           → Land(+0x298) ? Land : 5
    // 矿格靠这一段保住 ground=5：**不必** owns_land（原版 +0x2AC 只看
    // NoUseTileLandType，Tiberium=yes 的 GEM01-12/TIB01-12 是 false）。
    if (cell.overlay != -1) {
      const ra2::rmg::OverlayLandInfo ol = OverlayLandOf(cell.overlay);
      // 0x5FDD20：overlay==-1 → -1；否则先查 +0x2A9(Tiberium)，再查是否落在
      // TiberiumTypes 的 id 区间（RMG 的矿石 = 27..38 / 102..113）。
      const bool is_tib = ol.tiberium && ContainedTiberiumIndex(c) != -1;
      if (probe)
        std::fprintf(stderr,
            "[PROBE] tilepath tilelookup_ground=%d ov=%d ol.land=%d ol.tib=%d cti=%d is_tib=%d ramp=%d\n",
            ground, cell.overlay, ol.land, (int)ol.tiberium,
            ContainedTiberiumIndex(c), (int)is_tib, (int)cell.ramp);
      if (is_tib && cell.ramp > 4) {
        cell.overlay = -1;      // 0x47D849
        cell.overlayData = 0;   // 0x47D850
      } else if (is_tib) {
        ground = (ol.land != 0) ? ol.land : 5;  // 0x47D864-0x47D878
      }
    }
    // 0x47DB59-0x47DD2A：**有效路径的第二段 CliffBack**（CLI 之前缺失）。
    // SubTilePresent=true 时 RecalcAttributes 在 SetupLAT 之后、RecalcLandType
    // （0x47DD34 call 0x483C80）之前还会做一遍 6 方向邻居检查：
    //   0x47DD02 cmp byte [0x8871E0+0x664], 2    （CliffBackImpassability==2）
    //   0x47DD11 mov eax, [esi+0xEC]
    //   0x47DD17 test eax, eax ; je 0x47DD2A     （ground==0 → 写 3）
    //   0x47DD1B cmp eax, 2    ; je 0x47DD2A     （ground==2 → 写 3）
    //   0x47DD20 cmp eax, 6    ; je 0x47DD2A     （ground==6 → 写 3）
    //   0x47DD25 cmp eax, 8    ; je 0x47DD2A     （ground==8 → 写 3）
    //   0x47DD2A mov [esi+0xEC], 3
    // 即 ground ∈ {0,2,6,8} 且 6 方向任一邻居 level >= cell+4 → 强制 Rock(3)。
    // 6 方向与空洞路径第一段同款（0x47DB6C-0x47DD00 的 N/W/SE/E/SW/NE）。
    // 2026-10-03 动态实测：(49,54)/(49,55) tile=392（terrain=13 → ground=0）
    // 且 SE (51,56)/(51,57) lvl=8 → 原版写成 3；CLI 缺这段所以留 0。
    if (cliff_back_impassability_ == 2) {
      const int cell_lv = cell.level;
      bool cliff_back = false;
      const int kDir[6][2] = {{0, -1}, {-1, 0}, {2, 2},
                              {1, 1},  {-1, 1}, {1, -1}};
      for (const auto& d : kDir) {
        const ra2::rmg::CellCoord n{
            static_cast<std::int16_t>(c.x + d[0]),
            static_cast<std::int16_t>(c.y + d[1])};
        if (!CellExists(n)) continue;
        if (At(n).level >= cell_lv + 4) { cliff_back = true; break; }
      }
      if (cliff_back &&
          (ground == 0 || ground == 2 || ground == 6 || ground == 8)) {
        ground = 3;
      }
    }
    cell.ground = static_cast<std::int16_t>(ground);
    if (probe)
      std::fprintf(stderr, "[PROBE] tilepath final ground=%d\n", ground);
  }
  // 0x47DD34 call 0x483C80：+0x4C 全分支（overlay/占据者/地面三路）。
  cell.zoneId = static_cast<std::uint8_t>(ComputeLandType(c));
  if (probe) {
    const CliCell& cc = At(c);
    std::fprintf(stderr, "[PROBE] exit (%d,%d) ground=%d zone=%d iso=%d ov=%d\n",
        c.x, c.y, (int)cc.ground, (int)cc.zoneId, IsoTile(c), Overlay(c));
  }
}

// 0x483C80 全分支的输入装配：overlay 数据来自 rules 注入表（缺失时矿按
// 取证回落），占据者循环查 terrain_by_cell_（第一个树决定 2/5，非树
// 占据者跳过 → 0）。
int CliWorld::ComputeLandType(const ra2::rmg::CellCoord& c) const {
  const CliCell& cell = At(c);
  const bool within = ra2::rmg::IsWithinUsableArea(
      grid_.width(), local_, c.x, c.y, cell.level, cell.ramp, true);
  if (ground_foot_.empty()) {
    // 无 rules 数据（测试合成 world）时保持旧回落语义：只有出图判定。
    return within ? 0 : 7;
  }
  ra2::rmg::LandTypeInput in;
  in.within_usable_area = within;
  in.ground_type = cell.ground;
  if (cell.overlay != -1) {
    const ra2::rmg::OverlayLandInfo ol = OverlayLandOf(cell.overlay);
    in.has_overlay = true;
    in.overlay_land = ol.land;
    // OverlayType +0x22D/+0x2A8/+0x2B5/+0x2B4 四标志：RMG 出现的 overlay
    // （矿/石头/VEINHOLE）活体 zone 值反证全为 0，按 0 建模。
  }
  if (cell.object != 0) {
    const auto it = terrain_by_cell_.find(
        static_cast<std::uint32_t>(c.Packed()));
    if (it != terrain_by_cell_.end()) {
      const auto tb = terrain_occ_bits_.find(it->second);
      const int temp = (tb != terrain_occ_bits_.end()) ? tb->second.temperate : 7;
      const int snow = (tb != terrain_occ_bits_.end()) ? tb->second.snow : 7;
      // 原版 Scenario+0x1258==1 → SnowOccupationBits，否则 Temperate
      in.first_tree_occupation_bits = (theater_id_ == 1) ? snow : temp;
    }
  }
  return ra2::rmg::RecalcLandTypeFull(
      in, ground_foot_.empty() ? nullptr : ground_foot_.data());
}

ra2::rmg::OverlayLandInfo CliWorld::OverlayLandOf(int overlay) const {
  // ⚠️ 2026-10-14：id→小节的映射在 rules_loader 里按**条目出现次序**构建
  // （原版 0x668CE0/0x5FEC70 的行为），矿石区间 27-38/102-113 对应的
  // GEM01-12/TIB01-12 靠 INI 的 Tiberium=yes（loader 0x5FE8E7 把 Land 补成 5），
  // 无需在此硬编码。
  const auto it = overlay_land_.find(overlay);
  if (it != overlay_land_.end()) return it->second;
  // 表未注入（测试合成 world）：其余 overlay 回落 {0, 不占地}
  // （= 旧行为，走 tile 查表路径）。
  return {};
}

// ── 便捷访问 ────────────────────────────────────────────────────────────

}  // namespace cli
}  // namespace ra2
