// CLI 用的"完整 world"——同时实现流水线全部阶段需要的格子访问接口。
//
// 原版里每个阶段都直接读写同一批 CellClass 对象（+0x38 砖号 / +0x11A 子格 /
// +0x11B 层高 / +0x11C 坡面 / +0x44 overlay / +0x11E overlay 子帧 / +0xE4 object /
// +0xEC 地面类型 / +0x4C 寻路区号）与同一份 side² 条工作数组，所以复刻侧
// 用一个类把这些接口全部合并：一份实现覆盖 PaintWorld / ShoreWorld / TileWorld /
// RegionWorld / SmoothWorld / HillWorld / StartPointWorld / TechWorld / OreWorld /
// LatWorld 的全部纯虚。
//
// 接口清单（各头文件的取证注释不再重复）：
//   WaterStageWorld（stage_water.h）→ PaintWorld + SmoothWorld + ResetWorkOwners
//   MakingWorld（making_regions.h）→ RegionWorld + TileWorld + Ramp/Overlay/…
//   HillWorld（corner_lattice.h）→ TileWorld + Level/Ramp/HasObject/TileMorphable
//   StartPointWorld（start_points.h）→ 寻路区/waypoint/出生点
//   TechWorld（tech_buildings.h）→ 建筑（handle 语义，rules 数据注入）
//   OreWorld（ore_field.h）→ overlay/矿/地形/waypoint
//   LatWorld（lats.h）→ LAT 阶段的地形谓词与 SetupLAT/RecalcAttributes
//
// ⚠️ const/非 const 两份同名访问器：OreWorld/LatWorld/TechWorld 声明的是非 const
// 版本，其余接口是 const 版本。C++ 里它们构成重载而非覆盖，所以两类各写一份。
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "rmg/blob_paint.h"
#include "rmg/corner_lattice.h"
#include "rmg/lats.h"
#include "rmg/making_regions.h"
#include "rmg/ore_field.h"
#include "rmg/place_tile.h"
#include "rmg/rivers.h"
#include "rmg/start_points.h"
#include "rmg/stage_water.h"
#include "rmg/tech_buildings.h"
#include "rmg/theater.h"
#include "rmg/zones.h"

namespace ra2 {
namespace cli {

// 512×512 槽里一个格子的状态，字段偏移对应 CellClass（注释里的 +0x 是原版布局）。
struct CliCell {
  std::int16_t isoTile = 0;     // +0x38 IsoTileTypeIndex
  std::int8_t subtile = 0;      // +0x11A
  std::int8_t level = 0;        // +0x11B，有符号
  std::int8_t ramp = 0;         // +0x11C
  std::int8_t overlayData = 0;  // +0x11E，无符号
  std::int16_t overlay = -1;    // +0x44，-1 = 无 overlay
  std::int16_t object = 0;      // +0xE4，!= 0 = 有 object
  std::int16_t ground = 0;      // +0xEC GroundType
  std::uint8_t zoneId = 0;      // +0x4C 寻路区编号
  bool exists = false;          // 指针数组里这一格非空（菱形内）
};

// 输出端记录（.map 组装用）。坐标一律等距坐标，写 .map 时换算真实坐标。
struct TerrainRecord {
  std::string name;
  int x = 0, y = 0;
};
struct BuildingRecord {
  std::string type;    // 如 "CABHUT" / NeutralTechBuildings 表项
  int owner = 0;       // 0 = Neutral
  int x = 0, y = 0;    // 等距坐标
  int health = 256;
};
struct StartLocationRecord {
  int slot = 0;
  int x = 0, y = 0;
};

class CliWorld : public ra2::rmg::WaterStageWorld,
                 public ra2::rmg::MakingWorld,
                 public ra2::rmg::HillWorld,
                 public ra2::rmg::RiverWorld,
                 public ra2::rmg::StartPointWorld,
                 public ra2::rmg::TechWorld,
                 public ra2::rmg::OreWorld,
                 public ra2::rmg::LatWorld {
 public:
  // tiles：贴砖器用的砖表（assets::TheaterAssets::BuildRmgTileTable 产物，
  // 测试可给合成表）；work：side² 条工作数组，由调用方持有（跨阶段共享）。
  // tile_count = [0xA8ED38]（已装载砖总数），RecalcAttributes 的越界判定用。
  CliWorld(const ra2::rmg::MapGrid& grid, const ra2::rmg::TheaterData& theater,
           const std::vector<ra2::rmg::TileType>& tiles,
           ra2::rmg::Randomizer& rng,
           std::vector<ra2::rmg::PaintWorkCell>& work, int tile_count);

  // 阶段 1 的全图格子/工作数组初值（stage-init.md §四/§五）。
  // 必须先于任何阶段调用；可重复调用（回到初始状态）。
  void InitCells();

  // ── 输出记录（.map 组装用）──
  const std::vector<TerrainRecord>& terrains() const { return terrains_; }
  const std::vector<BuildingRecord>& buildings() const { return buildings_; }
  const std::vector<StartLocationRecord>& start_locations() const {
    return start_locations_;
  }
  // 阶段 6 的 waypoint 表；出生点从 0 号开始。
  const std::vector<ra2::rmg::CellCoord>& waypoints() const {
    return waypoints_;
  }
  // 阶段 8 的矿 overlay：读回来给 IsoMapPack5/OverlayPack 用
  int OverlayAt(const ra2::rmg::CellCoord& c) const { return At(c).overlay; }

  // 阶段 7 需要的 rules 数据（CLI 从 rulesmd.ini 注入）
  void set_neutral_tech_buildings(std::vector<std::string> v) {
    neutral_tech_buildings_ = std::move(v);
  }
  void set_foundations(
      std::map<std::string, std::vector<ra2::rmg::FoundationOffset>> v) {
    foundations_ = std::move(v);
  }
  // 只有具备对象注册链时，科技建筑才能通过 BuildingClass::Unlimbo。
  // 当前 CLI 仅有地图格状态，没有对象层，因此默认关闭。
  void set_tech_object_registration(bool enabled) {
    tech_object_registration_ = enabled;
  }
  // 阶段 8/10 需要的 rules 键（数据驱动，不硬编码）
  void set_tiberium_params(int min_t, int max_t) {
    tiberium_min_ = min_t;
    tiberium_max_ = max_t;
  }
  void set_max_trees(int v) { max_trees_ = v; }

  // 阶段 6 的寻路分区数据（pipeline 持有 ZoneTable，Rebuild 结果挂进来供
  // MovementZoneType 查询；对应原版 MapClass +0x18..+0x48 的全局区类型表）。
  void SetZoneData(const ra2::rmg::ZoneTable* zones,
                   const ra2::rmg::ZoneResult* result) {
    zones_ = zones;
    zone_result_ = result;
  }
  // rules.ini 12 个地面小节的 Foot= 值（RecalcLandType 用，数据驱动）。
  void set_ground_foot(std::vector<float> v) { ground_foot_ = std::move(v); }
  const std::vector<float>& get_ground_foot() const { return ground_foot_; }
  void set_cliff_back_impassability(int v) { cliff_back_impassability_ = v; }
  std::size_t loaded_tile_count() const { return loaded_tiles_.size(); }
  bool has_loaded_tile(int t) const { return loaded_tiles_.count(t) > 0; }
  // TileTypeClass +0x2E0 == 0 的砖号集合（疑似 Morphable，键名未确认；
  // 默认空 = 全部可变形，e2e 对拍时从真实数据校准）。
  void set_nonmorph_tiles(std::set<int> v) { nonmorph_ = std::move(v); }

  // ── 全接口实现（分节声明，定义在 cliworld.cpp）──

  // PaintWorld
  bool IsPaintable(const ra2::rmg::CellCoord& c) const override;   // 0x4865D0
  int IsoTile(const ra2::rmg::CellCoord& c) const override;
  void SetIsoTile(const ra2::rmg::CellCoord& c, int v) override;
  void SetCellSubTile(const ra2::rmg::CellCoord& c, int v) override;
  void SetCellLevel(const ra2::rmg::CellCoord& c, int v) override;
  bool SmoothShoreline(int blob_id) override;   // Shoreline(blob, 1)

  // ShoreWorld
  bool WorkValid(const ra2::rmg::CellCoord& c) const override;
  int MaskCache(const ra2::rmg::CellCoord& c) const override;

  // TileWorld
  bool CellExists(const ra2::rmg::CellCoord& c) const override;
  int SubTile(const ra2::rmg::CellCoord& c) const override;
  void SetSubTile(const ra2::rmg::CellCoord& c, int v) override;
  void SetLevel(const ra2::rmg::CellCoord& c, int v) override;
  void SetRamp(const ra2::rmg::CellCoord& c, int v) override;
  void SetDummyCoord(const ra2::rmg::CellCoord& c) override;
  int DummyIsoTile() const override;
  int DummySubTile() const override;
  void SetDummyTile(int tile, int subtile, int level) override;
  void SetDummyIsoTile(int tile) override;
  int WorkBlob(const ra2::rmg::CellCoord& c) const override;
  void SetWorkBlob(const ra2::rmg::CellCoord& c, int v) override;

  // SmoothWorld
  void SetMaskCache(const ra2::rmg::CellCoord& c, int v) override;
  void ResetMaskCaches() override;
  int Level(const ra2::rmg::CellCoord& c) const override;
  ra2::rmg::CellCoord DummyCoord() const override;
  const ra2::rmg::TileType* TileByIndex(int index) const override;

  // WaterStageWorld
  void ResetWorkOwners() override;

  // RegionWorld
  void SetSubTileR(const ra2::rmg::CellCoord& c, int v);  // 壳子

  // MakingWorld
  int Ramp(const ra2::rmg::CellCoord& c) const override;
  int Overlay(const ra2::rmg::CellCoord& c) const override;
  void SetOverlay(const ra2::rmg::CellCoord& c, int v) override;
  int OverlayData(const ra2::rmg::CellCoord& c) const override;
  void SetOverlayData(const ra2::rmg::CellCoord& c, int v) override;
  void PlaceRepairHut(const ra2::rmg::CellCoord& c) override;

  // HillWorld
  int LevelH(const ra2::rmg::CellCoord& c) const;   // 壳子
  int RampH(const ra2::rmg::CellCoord& c) const;    // 壳子
  bool HasObject(const ra2::rmg::CellCoord& c) const override;
  bool TileMorphable(int tile) const override;
  int DummyLevel() const override;
  int DummyRamp() const override;
  void SetDummyLevel(int v) override;
  void SetDummyRamp(int v) override;

  // StartPointWorld
  int CellZoneId(const ra2::rmg::CellCoord& c) const override;
  int MovementZoneType(const ra2::rmg::CellCoord& c,
                       int movement_zone) const override;
  int IsoTileSP(const ra2::rmg::CellCoord& c) const;   // 壳子
  int LevelSP(const ra2::rmg::CellCoord& c) const;     // 壳子
  int RampSP(const ra2::rmg::CellCoord& c) const;      // 壳子
  ra2::rmg::CellCoord ReadBackCoord(const ra2::rmg::CellCoord& c) override;
  void SetWaypoint(int index, const ra2::rmg::CellCoord& c) override;
  void ResetStartLocationTable() override;
  void SetStartLocation(int slot, const ra2::rmg::CellCoord& c) override;
  void PlaceRandomBuilding(ra2::rmg::RegionClass* r) override;

  // TechWorld
  int LevelT(const ra2::rmg::CellCoord& c);   // 壳子
  bool IsPlain(const ra2::rmg::CellCoord& c) override;
  bool HasOccupier(const ra2::rmg::CellCoord& c) override;
  int Field0xEC(const ra2::rmg::CellCoord& c) override;
  bool WithinUsableArea(const ra2::rmg::CellCoord& c) override;
  int NeutralTechBuildingCount() override;
  bool AllocBuildingMemory() override;
  int CreateBuilding(int type_index) override;
  const std::vector<ra2::rmg::FoundationOffset>& Foundation(
      int handle) override;
  bool Unlimbo(int handle, const ra2::rmg::CellCoord& at) override;
  void Destroy(int handle) override;

  // OreWorld（非 const 重载）
  int IsoTile(const ra2::rmg::CellCoord& c) override;
  int Level(const ra2::rmg::CellCoord& c) override;
  int Ramp(const ra2::rmg::CellCoord& c) override;
  int Overlay(const ra2::rmg::CellCoord& c) override;
  int OverlayData(const ra2::rmg::CellCoord& c) override;
  int ContainedTiberiumIndex(const ra2::rmg::CellCoord& c) override;
  void CreateTerrain(const char* name,
                     const ra2::rmg::CellCoord& at) override;
  ra2::rmg::CellCoord Waypoint(int index) override;

  // LatWorld（非 const 重载）
  void SetIsoTileLat(const ra2::rmg::CellCoord& c, int v);  // 壳子
  void SetOverlayLat(const ra2::rmg::CellCoord& c, int v);  // 壳子
  int Field0xE4(const ra2::rmg::CellCoord& c) override;
  bool IsPaveTile(const ra2::rmg::CellCoord& c) override;
  bool IsMiscPaveTile(const ra2::rmg::CellCoord& c) override;
  bool IsShorePiece(const ra2::rmg::CellCoord& c) override;
  bool IsClearToSandLat(const ra2::rmg::CellCoord& c) override;
  bool IsGreen(const ra2::rmg::CellCoord& c) override;
  void SetupLAT(const ra2::rmg::CellCoord& c) override;
  void RecalcAttributes(const ra2::rmg::CellCoord& c, int arg) override;

  // 便捷访问（读最终地形状态，IsoMapPack5/OverlayPack 组装用）
  const CliCell& CellAt(const ra2::rmg::CellCoord& c) const { return At(c); }
  int slot_count() const { return kCellSlots; }
  bool Exists(int slot) const { return exists_[slot] != 0; }
  // 整表导出（pipeline 收尾时拷给调用方；CliWorld 持有 rng/work 引用，
  // 生命周期跟着 pipeline 走，格子弹出去后原对象就销毁了）。
  const std::vector<CliCell>& cells() const { return cells_; }
  // ── 对拍专用（_emu 注入）：pipeline 之外的验证脚本要能直接灌格子状态。
  std::vector<CliCell>& cells_mutable() { return cells_; }
  void SetExist(int x, int y, bool v);
  const std::vector<char>& exists() const { return exists_; }
  // 可用区矩形（LocalSize {2,5,W,H} 夹取后），各阶段 Context 共用。
  const ra2::rmg::MapRect& usable_rect() const { return local_; }
  int side() const { return grid_.side(); }

 private:
  static constexpr int kCellSlots = 0x40000;   // 512×512
  int SlotOf(const ra2::rmg::CellCoord& c) const;
  std::size_t WorkIdx(const ra2::rmg::CellCoord& c) const;
  // 菱形外退到共享哑元（0xABDC50）
  const CliCell& At(const ra2::rmg::CellCoord& c) const;
  CliCell& Mut(const ra2::rmg::CellCoord& c);
  // 读砖表的辅助（越界/空洞处理），RecalcAttributes 用
  const ra2::rmg::TileSubCell* SubCellOf(int tile, int subtile) const;

  ra2::rmg::MapGrid grid_;
  const ra2::rmg::TheaterData& theater_;
  ra2::rmg::Randomizer& rng_;
  std::vector<ra2::rmg::PaintWorkCell>& work_;
  std::vector<CliCell> cells_;
  std::vector<char> exists_;
  std::map<int, ra2::rmg::TileType> tiles_;
  CliCell dummy_;
  ra2::rmg::CellCoord dummy_coord_;

  // rules 注入
  std::vector<std::string> neutral_tech_buildings_;
  std::map<std::string, std::vector<ra2::rmg::FoundationOffset>> foundations_;
  std::vector<ra2::rmg::FoundationOffset> foundation_;   // 当前建筑占地表
  int tiberium_min_ = 2500;
  int tiberium_max_ = 5500;
  int max_trees_ = 500;
  int waypoint_scale_ = 0;

  // 输出记录
  std::vector<TerrainRecord> terrains_;
  std::vector<BuildingRecord> buildings_;
  std::vector<StartLocationRecord> start_locations_;
  std::vector<ra2::rmg::CellCoord> waypoints_;   // 索引 = waypoint 号
  std::vector<int> active_buildings_;            // handle 栈（记录顺序）
  bool tech_object_registration_ = false;

  // 阶段 5/6/10 需要的引擎数据
  int tile_count_ = 0;             // [0xA8ED38]
  ra2::rmg::MapRect local_;        // 可用区矩形（Set_LocalSize {2,5,W,H} 夹取后）
  const ra2::rmg::ZoneTable* zones_ = nullptr;
  const ra2::rmg::ZoneResult* zone_result_ = nullptr;
  std::vector<float> ground_foot_;  // 12 个地面小节的 Foot=，空 = 全 0
  int cliff_back_impassability_ = 2;  // [General] CliffBackImpassability
  std::set<int> loaded_tiles_;     // 模拟原版 .tmp lazy load：阶段 2/4 写入的 tile
  std::set<int> nonmorph_;          // TileTypeClass +0x2E0 == 0 的砖
};

}  // namespace cli
}  // namespace ra2
