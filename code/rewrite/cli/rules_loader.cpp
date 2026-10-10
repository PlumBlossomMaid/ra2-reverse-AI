// rules_loader 实现 —— 见 rules_loader.h。
#include "rules_loader.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "rmg/zones.h"

namespace ra2 {
namespace cli {

namespace {

std::string Trim(const std::string& s) {
  std::size_t a = 0, b = s.size();
  auto ws = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (a < b && ws(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && ws(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

bool IEquals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

std::vector<std::string> SplitCsv(const std::string& s) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < s.size()) {
    std::size_t j = s.find(',', i);
    if (j == std::string::npos) j = s.size();
    const std::string tok = Trim(s.substr(i, j - i));
    if (!tok.empty()) out.push_back(tok);
    i = j + 1;
  }
  return out;
}

// 12 个小节名，顺序 = GroundType 枚举（kGroundClear..kGroundWeeds）
const char* const kGroundSections[ra2::rmg::kGroundTypeCount] = {
    "Clear",     "Road",  "Water", "Rock",  "Wall",      "Tiberium",
    "Beach",     "Rough", "Ice",   "Railroad", "Tunnel", "Weeds",
};

int GroundIndexOf(const std::string& name) {
  for (int g = 0; g < ra2::rmg::kGroundTypeCount; ++g) {
    if (IEquals(name, kGroundSections[g])) return g;
  }
  return 0;
}

bool IsTrue_(const std::string& v) {
  return IEquals(v, "yes") || IEquals(v, "true") || v == "1";
}

bool AllDigits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

}  // namespace

bool LoadRmgRules(const std::string& text, RmgRules* out) {
  std::istringstream in(text);
  std::string line, section;
  out->ground_foot.assign(ra2::rmg::kGroundTypeCount, 0.0f);

  // overlay/terrain 数据需要两趟：先收集各小节的相关键，
  // 再把 [OverlayTypes] 列表的条目按**出现次序**映射成 overlay id。
  struct OverlaySec {
    bool has_land = false;
    int land = 0;
    bool tiberium = false;
    // ⚠️ 原版 OverlayTypeClass 构造函数（0x5FE250，0x5FE25E `mov eax,1` /
    // 0x5FE271 `mov [esi+0x2ac],al`）把 +0x2AC（NoUseTileLandType）默认成
    // **1（true）**——INI 没显式写 NoUseTileLandType=no 的 overlay（含全部
    // TIB/GEM 矿）都进 RecalcAttributes 的 overlay 专属路径（0x47D333，
    // cliff_back 变体 0x47D53E 无条件写 ground=3）。
    // CLI 旧版默认 false（只在 INI 有该键时才设置）是错的。
    bool no_use_tile = true;
  };
  std::map<std::string, OverlaySec> overlay_secs;
  std::vector<std::string> overlay_list;  // [OverlayTypes] 条目名，按出现次序

  while (std::getline(in, line)) {
    std::size_t semi = line.find(';');
    if (semi != std::string::npos) line = line.substr(0, semi);
    line = Trim(line);
    if (line.empty()) continue;
    if (line.front() == '[') {
      std::size_t r = line.find(']');
      section = (r == std::string::npos) ? line.substr(1) : line.substr(1, r - 1);
      continue;
    }
    std::size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = Trim(line.substr(0, eq));
    const std::string val = Trim(line.substr(eq + 1));

    if (IEquals(section, "General")) {
      if (IEquals(key, "RMGMinimumTiberium")) {
        out->min_tiberium = std::atoi(val.c_str());
      } else if (IEquals(key, "RMGMaximumTiberium")) {
        out->max_tiberium = std::atoi(val.c_str());
      } else if (IEquals(key, "MaxTrees")) {
        out->max_trees = std::atoi(val.c_str());
      } else if (IEquals(key, "CliffBackImpassability")) {
        out->cliff_back_impassability = std::atoi(val.c_str());
      } else if (IEquals(key, "NeutralTechBuildings")) {
        out->neutral_tech_buildings = SplitCsv(val);
      }
      continue;
    }
    if (IEquals(section, "AI")) {
      if (IEquals(key, "NeutralTechBuildings")) {
        out->neutral_tech_buildings = SplitCsv(val);
      }
      continue;
    }
    if (IEquals(section, "OverlayTypes")) {
      // 列表行：数字键=类名。值里第一个空格之后全是行尾杂注（原版
      // INIClass 在 ';' 处截断），类名取首词。
      if (AllDigits(key)) {
        std::string first = val;
        const std::size_t sp = first.find_first_of(" \t");
        if (sp != std::string::npos) first = first.substr(0, sp);
        overlay_list.push_back(first);
      }
      continue;
    }
    // 各小节键
    if (IEquals(key, "Land")) {
      overlay_secs[section].has_land = true;
      overlay_secs[section].land = GroundIndexOf(val);
      continue;
    }
    if (IEquals(key, "Tiberium")) {
      overlay_secs[section].tiberium = IsTrue_(val);
      continue;
    }
    if (IEquals(key, "NoUseTileLandType")) {
      overlay_secs[section].no_use_tile = IsTrue_(val);
      continue;
    }
    if (IEquals(key, "TemperateOccupationBits")) {
      out->terrain_occ[section].temperate = std::atoi(val.c_str());
      continue;
    }
    if (IEquals(key, "SnowOccupationBits")) {
      out->terrain_occ[section].snow = std::atoi(val.c_str());
      continue;
    }
    if (!IEquals(key, "Foot")) continue;
    for (int g = 0; g < ra2::rmg::kGroundTypeCount; ++g) {
      if (IEquals(section, kGroundSections[g])) {
        out->ground_foot[static_cast<std::size_t>(g)] =
            static_cast<float>(std::atof(val.c_str()));
        break;
      }
    }
  }

  // 第二趟：overlay id → 小节名 → OverlayLandInfo
  // ⚠️ 2026-10-14 修正：原版 OverlayTypeClass 表的下标**不是** [OverlayTypes]
  // 列表里的数字键，而是**条目在列表中的出现次序**（0 起）。实锤链：
  //   0x668CE0-0x668D32 循环 EntryCount 次，逐条 GetEntryName(i) 调 0x5FEC70；
  //   0x5FEC70 先按名字查表（0xA83D84/0xA83D90），命中返回旧项、否则 new 一个
  //   0x2BC 对象追加到表尾——下标 = 表长，与键值无关。
  //   名字为 "none"/"<none>" 的条目 0x5FEC70 直接返回 0（0x668D22 跳过），
  //   不占下标。
  // 实证：本 mod 列表键 27=NAWALL、102-104=LOBRDG26-28、168=TIB3_19，但活体
  // 矿格 ov=27-38/102-113 恒 ground=5/zone=0、石头 ov=168-177 恒
  // ground=3/zone=6。按下标映射：27-38=GEM01-12、102-113=TIB01-12（均
  // Tiberium=yes → Land=5），168-177=SROCK01-05/TROCK01-05（Land=Rock +
  // NoUseTileLandType=true）——与活体完全吻合；按键映射则 168=TIB3_19
  // （Tiberium=yes → Land=5）与活体 ground=3 矛盾。
  // 原版硬编码的矿/石头区间（CreateOreField base 27/102 + PickBelow(12)、
  // 撒石头 0xA8/0xAD + PickBelow(5)）也正是按下标算的。
  {
    std::map<std::string, bool> seen;
    int next_id = 0;
    for (const std::string& nm : overlay_list) {
      if (IEquals(nm, "none") || IEquals(nm, "<none>")) continue;  // 0x5FEC70 返回 0
      if (seen.find(nm) != seen.end()) continue;                  // 命中旧项，不占下标
      seen[nm] = true;
      const int oid = next_id++;
      const auto sit = overlay_secs.find(nm);
      const bool tib = (sit != overlay_secs.end()) && sit->second.tiberium;
      const bool ntl = (sit != overlay_secs.end()) && sit->second.no_use_tile;
      const bool has_land =
          (sit != overlay_secs.end()) && sit->second.has_land;
      ra2::rmg::OverlayLandInfo info;
      info.tiberium = tib;
      info.land = has_land ? sit->second.land : 0;
      // 0x5FE8C9-0x5FE8EF：Tiberium=yes 且 Land 缺省（==0）→ Land=5。
      if (tib && info.land == 0) info.land = 5;
      // 0x47D325 读的 +0x2AC 就是 NoUseTileLandType（0x5FE958-0x5FE973），
      // **不含** Tiberium——Tiberium 是 +0x2A9（0x5FE7EB-0x5FE805）。
      // 但 +0x2AC 构造默认 = 1（0x5FE250 构造函数 0x5FE25E `mov eax,1` /
      // 0x5FE271 `mov [esi+0x2ac],al`），只有 INI 显式写
      // NoUseTileLandType=no 才翻成 false；TIB/GEM 矿全不写该键 →
      // 恒 true → 走 overlay 专属路径（0x47D333，cliff_back 变体
      // 0x47D53E 无条件写 ground=3）。默认值修正见上方 OverlaySec 定义。
      info.owns_land = ntl || info.land == 4 || info.land == 9;
      out->overlay_land[oid] = info;
    }
  }
  return true;
}

bool LoadRmgRulesFile(const std::string& path, RmgRules* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  return LoadRmgRules(ss.str(), out);
}

namespace {

// Foundation 字符串 → 占地偏移（0x89C900 静态表的内容，静态初始化器
// 0x45B1C0 实锤）。普通 "WxH" 全是 row-major 矩形、锚点在左上角；
// 两个特殊形状照抄原表。返回 false = 字符串不在枚举表里（原版此时 kind
// 取 0 = 1x1，由调用方决定不放条目、走 1x1 回退）。
bool ExpandFoundation(const std::string& s,
                      std::vector<ra2::rmg::FoundationOffset>* out) {
  out->clear();
  if (IEquals(s, "0x0")) return true;   // 空表（原版 0x0 直接是收尾哨兵）
  if (IEquals(s, "3x3Refinery")) {
    // 唯一带洞的形状：缺 (2,1)
    static const int kCells[8][2] = {{0, 0}, {1, 0}, {2, 0}, {0, 1},
                                     {1, 1}, {0, 2}, {1, 2}, {2, 2}};
    for (const auto& c : kCells) out->push_back({c[0], c[1]});
    return true;
  }
  // "WxH"：W/H 都是一位数字（枚举表里最大 6x4）
  if (s.size() == 3 && std::isdigit(static_cast<unsigned char>(s[0])) &&
      (s[1] == 'x' || s[1] == 'X') &&
      std::isdigit(static_cast<unsigned char>(s[2]))) {
    const int w = s[0] - '0', h = s[2] - '0';
    if (w < 1 || h < 1) return false;
    for (int dy = 0; dy < h; ++dy)
      for (int dx = 0; dx < w; ++dx) out->push_back({dx, dy});
    return true;
  }
  return false;
}

}  // namespace

void LoadArtFoundations(const std::string& art_text,
                        const std::vector<std::string>& type_names,
                        RmgRules* out) {
  // 先按节名收集 Foundation= 键
  std::map<std::string, std::string> fnd_keys;
  {
    std::istringstream in(art_text);
    std::string line, section;
    while (std::getline(in, line)) {
      std::size_t semi = line.find(';');
      if (semi != std::string::npos) line = line.substr(0, semi);
      line = Trim(line);
      if (line.empty()) continue;
      if (line.front() == '[') {
        std::size_t r = line.find(']');
        section =
            (r == std::string::npos) ? line.substr(1) : line.substr(1, r - 1);
        continue;
      }
      std::size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      if (!IEquals(Trim(line.substr(0, eq)), "Foundation")) continue;
      fnd_keys[section] = Trim(line.substr(eq + 1));
    }
  }
  for (const std::string& type : type_names) {
    for (const auto& [sec, val] : fnd_keys) {
      if (!IEquals(sec, type)) continue;
      std::vector<ra2::rmg::FoundationOffset> cells;
      if (!ExpandFoundation(val, &cells)) break;   // 未知形状：留 1x1 回退
      cells.push_back({0x7FFF, 0x7FFF});           // 原版列表的收尾哨兵
      out->foundations[type] = std::move(cells);
      break;
    }
  }
}

}  // namespace cli
}  // namespace ra2
