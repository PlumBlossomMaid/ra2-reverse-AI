// CliWorld 实现——见 cliworld.h 头部注释。
//
// 一份格子状态 + 一份工作数组，同时喂给流水线全部阶段。菱形外的格子统一退到
// 共享哑元 dummy_（原版 0xABDC50 语义），上一次留在哑元里的字段会被下一次读到。
#include "cliworld.h"

#include <algorithm>
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
    // 原版阶段 1（stage-init.md §四）只重置上面 6 个字段，不碰
    // +0xE4(object)/+0xEC(ground)/+0x4C(zoneId)——真实游戏连续生成时这三个
    // 字段的上一局残留会带入下一局（脏局）。全新进程里它们默认就是 0，
    // 首局行为不变；回放脏局（--init-cells）时残留必须保留。
  });
  // 工作数组（stage-init.md §五）：全零，但 +0x4A = 1（有效位）、+0x40 = -1
  // （掩码缓存未计算）。
  for (ra2::rmg::PaintWorkCell& w : work_) {
    w = ra2::rmg::PaintWorkCell{};
    w.flag4a = true;
    w.mask_cache = -1;
  }
  // 再扫一遍：打包坐标写进记录头 4 字节，+0x11B 改成 MapSeed +0x30C（= 4）。
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
  Mut(at).object = 1;   // 原版 0x71BB90 置 cell +0xE4，供 GrowTreePatch 种植检查
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
  if (!empty_path) SetupLAT(c);

  CliCell& cell = Mut(c);
  // 0x47D2F6-0x47D318 + 0x47D818-0x47D849：矿石 overlay（ContainedTiberiumIndex
  // != -1）在函数**开头**无条件写 ground = OverlayTypeClass[ov]+0x298 = 5，
  // 矿格（iso=0）走空路径时 tile 号被清为 0xFFFF 但 ground 保留 overlay 值
  // ——原版 0x47D336 `cmp eax,0xFFFF` 命中 → 跳去 0x47D364 更新 ramp，
  // 不走 0x47D57B 砖号路径（否则会把 ground 清零）。所以必须**在空路径
  // 判定之前**写 ground=5，否则矿格进 empty_path 分支被 cell.ground=0 覆盖。
  if (ContainedTiberiumIndex(c) != -1) {
    cell.ground = 5;  // 矿石 overlay（Riparius 102-113 / Cruentus 27-38）
  }
  // 0x47D54A 之前那段 +0xEC 的重算：从砖表子块头 +0x29 查 TerrainToGround。
  // 有效路径（SubTilePresent=true，即 .tmp 已加载）走 0x544BE0；空洞路径 ground=0。
  if (empty_path) {
    // ⚠️ 空路径（原版 0x47D5E6-0x47D5F9）：SubTilePresent=false 时
    // 清 tile=0xFFFF / ground=0 / ramp=0，再走 CliffBack 第一段。
    // 但矿格已在函数开头被 overlay 分支写 ground=5（0x47D30F），
    // 且走 0x47D336 tile==0xFFFF 分支（不走 0x47D57B），保留 overlay 值。
    if (ContainedTiberiumIndex(c) == -1) {
      cell.ground = 0;
    }
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
        // 原版两条 CliffBack 出口的写 3 条件不同：
        //   矿格走 overlay 分支 → 0x47D52F `cmp 2 / jne / mov [0xEC],3`
        //     **无条件**写 3（会覆盖开场写的 ground=5，权威 1/316 矿格被覆盖）
        //   普通空路径 → 0x47D7A9 先 `test [0xEC]`，ground==0 才写
        //     （空路径 ground 恒 0，检查形同虚设）
        // CLI 用 ContainedTiberiumIndex 区分两条路径。
        if (ContainedTiberiumIndex(c) != -1 || cell.ground == 0) {
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
    // 0x47D2F6-0x47D318 + 0x47D820-0x47D849：矿石 overlay（ContainedTiberiumIndex
    // != -1）走特殊分支——0x47D30F 写 ground = OverlayTypeClass[ov]+0x298 = 5，
    // 之后 0x47D82A `cmp byte [esi+0x11c], 4` 判坡道非 4 时保留，跳过
    // 0x544BE0 的 terrain 查表覆盖。非矿石 ov（如石头 168-173，+0x2AC=0）
    // 走 0x47D87A 的 0x544BE0 查表路径，ground 由 .tmp terrain 决定，
    // 不保留 overlay 值，故此处不处理。
    if (ContainedTiberiumIndex(c) != -1) {
      ground = 5;  // 矿石 overlay（Riparius 102-113 / Cruentus 27-38）
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
    // ⚠️ overlay 分支（矿/石头 overlay 走 0x47D333 专用分支，CLI 因 tile 0
    // 有 .tmp 而落入本 else）的 CliffBack 落点是 **0x47D52F**，写 3 **不看
    // ground 值**——矿格开场 ground=5 也会被覆盖（权威 316 矿格里恰好 1 格
    // slot 33841 ov=112 被覆盖成 3，即悬崖脚下）。普通有效路径 0x47DD2A 才
    // 要求 ground ∈ {0,2,6,8}。→ 矿格（ContainedTiberiumIndex != -1）直接
    // 加入写 3 条件，不查 ground。
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
          (ContainedTiberiumIndex(c) != -1 || ground == 0 || ground == 2 ||
           ground == 6 || ground == 8)) {
        ground = 3;
      }
    }
    cell.ground = static_cast<std::int16_t>(ground);
  }
  // 0x483C80：地面类别。出图判定用 IsWithinUsableArea(use_height=true)。
  // ⚠️ 矿格走 0x47D52F 出口——**不做** RecalcLandType（0x483C80 前面那个
  // call 0x47D54A 是 SetupLAT），+0x4C 保留进入本函数前的旧值。CLI 在
  // 阶段 1 清格子时把每格 +0x4C 清成 0，之后阶段 6 只用它查 zone，不会
  // 再写；原版这里读出来的旧值对阶段 6 无影响（阶段 6 只看 +0x4C 是否
  // 属于某区）。所以矿格 zoneId 保持 0（等效于原版旧值 0），直接 return。
  if (cell.ground == 5) {
    return;
  }
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
