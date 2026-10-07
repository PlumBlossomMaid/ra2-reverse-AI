// rulesmd.ini 里随机地图生成器依赖的键（数据驱动，不硬编码默认值以外的任何数）。
//
// 键名（全部来自原版 RulesClass 装载点取证）：
//   [General] RMGMinimumTiberium / RMGMaximumTiberium / MaxTrees
//   [AI]      NeutralTechBuildings（逗号分隔建筑名表）
//   12 个地面小节（GroundType 顺序，zones.h 的枚举）的 Foot=
//   [OverlayTypes] 各 overlay 小节的 Land= / Tiberium= / NoUseTileLandType=
//   各 TerrainType 小节的 TemperateOccupationBits= / SnowOccupationBits=
#pragma once

#include <map>
#include <string>
#include <vector>

#include "rmg/tech_buildings.h"
#include "rmg/zones.h"

namespace ra2 {
namespace cli {

struct RmgRules {
  int min_tiberium = 2500;   // [General] RMGMinimumTiberium
  int max_tiberium = 5500;   // [General] RMGMaximumTiberium
  int max_trees = 500;       // [General] MaxTrees
  int cliff_back_impassability = 2;  // [General] CliffBackImpassability (0/1/2)
  std::vector<std::string> neutral_tech_buildings;  // [AI] NeutralTechBuildings
  std::map<std::string, std::vector<ra2::rmg::FoundationOffset>> foundations;
  // 12 个地面小节 Foot=，顺序 = GroundType 枚举（kGroundClear..kGroundWeeds）。
  // 缺键的小节补 0。
  std::vector<float> ground_foot;
  // [OverlayTypes] 每 id 的占地数据（RecalcAttributes 0x47D2F6-0x47D32D 用）：
  // land = OverlayTypeClass +0x298（Land= 键的 GroundType；Tiberium=yes 且无
  // Land= 键时 = 5，活体矿格 ground=5 实锤）；
  // owns_land = land∈{4,9} || Tiberium=yes || NoUseTileLandType=true
  // （原版 +0x2AC 判定的 INI 口径：走 overlay 专属路径，ground 不被 tile
  // 查表覆盖——活体石头格 iso=423 仍 ground=3 实锤）。
  std::map<int, ra2::rmg::OverlayLandInfo> overlay_land;
  // [TerrainTypes] 每树名 OccupationBits（RecalcLandType 0x483DDF 用）：
  // 温带查 TemperateOccupationBits==7、雪地查 SnowOccupationBits==7 → zone 2，
  // 否则 5。键缺省 = 7（活体 TIBTRE 格 zone=2 实锤，TIBTRE 小节无此键）。
  std::map<std::string, ra2::rmg::TerrainOccBits> terrain_occ;
};

// 解析 rulesmd.ini 文本。解析器与原版 INIClass 的宽容度一致：
// ';' 注释、节名/键名大小写不敏感、键值行首尾去空白。
bool LoadRmgRules(const std::string& text, RmgRules* out);

// 解析 art(md).ini 文本里各建筑小节的 Foundation=，展开成占地偏移表
// （原版 BuildingTypeClass::LoadFromINI 读 art 的 Foundation 键，经字符串→
// 枚举表（1x1=0 … 6x4=20、3x3Refinery=9、0x0=21）索引 0x89C900 的静态占地
// 表——row-major、左上角为锚点、3x3Refinery 缺 (2,1)、0x0 为空表）。
// 只对 type_names 列出的类型取键；同一类型后读的文本覆盖先读的（artmd 盖 art）。
void LoadArtFoundations(const std::string& art_text,
                        const std::vector<std::string>& type_names,
                        RmgRules* out);

bool LoadRmgRulesFile(const std::string& path, RmgRules* out);

}  // namespace cli
}  // namespace ra2
