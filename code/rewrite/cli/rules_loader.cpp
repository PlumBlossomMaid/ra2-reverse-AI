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

std::string LastToken(const std::string& s) {
  const std::size_t p = s.find_last_of(" \t");
  return (p == std::string::npos) ? s : s.substr(p + 1);
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

  // overlay/terrain 数据需要两趟：先收集全部小节名与各小节的相关键，
  // 再解析 [OverlayTypes] 列表的 id → 小节名（列表值可能带行尾杂注，
  // 如 `170=USELESS; gs ... VEINHOLE`——首词无小节时取原始行尾词，
  // 活体取证 overlay 170 的行为与 [VEINHOLE] 完全一致：Land=Rock +
  // NoUseTileLandType=true）。
  struct OverlaySec {
    bool has_land = false;
    int land = 0;
    bool tiberium = false;
    bool no_use_tile = false;
  };
  std::map<std::string, OverlaySec> overlay_secs;
  std::map<int, std::pair<std::string, std::string>> overlay_list;  // id → (首词, 行尾词)
  std::map<std::string, bool> sections_seen;

  while (std::getline(in, line)) {
    const std::string raw = Trim(line);
    std::size_t semi = line.find(';');
    if (semi != std::string::npos) line = line.substr(0, semi);
    line = Trim(line);
    if (line.empty()) continue;
    if (line.front() == '[') {
      std::size_t r = line.find(']');
      section = (r == std::string::npos) ? line.substr(1) : line.substr(1, r - 1);
      sections_seen[section] = true;
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
      // 列表行：id=名字（key 全数字）
      if (AllDigits(key)) {
        std::string first = val;
        const std::size_t sp = first.find_first_of(" \t");
        if (sp != std::string::npos) first = first.substr(0, sp);
        overlay_list[std::atoi(key.c_str())] = {first, LastToken(raw)};
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
  for (const auto& [id, names] : overlay_list) {
    std::string nm = names.first;
    if (sections_seen.find(nm) == sections_seen.end() &&
        sections_seen.find(names.second) != sections_seen.end()) {
      nm = names.second;
    }
    ra2::rmg::OverlayLandInfo info;
    const auto sit = overlay_secs.find(nm);
    const bool tib = (sit != overlay_secs.end()) && sit->second.tiberium;
    const bool ntl = (sit != overlay_secs.end()) && sit->second.no_use_tile;
    const bool has_land =
        (sit != overlay_secs.end()) && sit->second.has_land;
    info.land = has_land ? sit->second.land : (tib ? 5 : 0);
    info.owns_land = tib || ntl || info.land == 4 || info.land == 9;
    out->overlay_land[id] = info;
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

}  // namespace cli
}  // namespace ra2
