// rulesmd.ini 里随机地图生成器依赖的键（数据驱动，不硬编码默认值以外的任何数）。
//
// 键名（全部来自原版 RulesClass 装载点取证）：
//   [General] RMGMinimumTiberium / RMGMaximumTiberium / MaxTrees /
//             NeutralTechBuildings（逗号分隔建筑名表）
//   12 个地面小节（GroundType 顺序，zones.h 的枚举）的 Foot=
#pragma once

#include <string>
#include <vector>

namespace ra2 {
namespace cli {

struct RmgRules {
  int min_tiberium = 2500;   // [General] RMGMinimumTiberium
  int max_tiberium = 5500;   // [General] RMGMaximumTiberium
  int max_trees = 500;       // [General] MaxTrees
  std::vector<std::string> neutral_tech_buildings;  // [General] NeutralTechBuildings
  // 12 个地面小节 Foot=，顺序 = GroundType 枚举（kGroundClear..kGroundWeeds）。
  // 缺键的小节补 0。
  std::vector<float> ground_foot;
};

// 解析 rulesmd.ini 文本。解析器与原版 INIClass 的宽容度一致：
// ';' 注释、节名/键名大小写不敏感、键值行首尾去空白。
bool LoadRmgRules(const std::string& text, RmgRules* out);

bool LoadRmgRulesFile(const std::string& path, RmgRules* out);

}  // namespace cli
}  // namespace ra2
