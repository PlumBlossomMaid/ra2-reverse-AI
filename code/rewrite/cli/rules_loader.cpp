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

}  // namespace

bool LoadRmgRules(const std::string& text, RmgRules* out) {
  std::istringstream in(text);
  std::string line, section;
  out->ground_foot.assign(ra2::rmg::kGroundTypeCount, 0.0f);

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
    if (!IEquals(key, "Foot")) continue;
    for (int g = 0; g < ra2::rmg::kGroundTypeCount; ++g) {
      if (IEquals(section, kGroundSections[g])) {
        out->ground_foot[static_cast<std::size_t>(g)] =
            static_cast<float>(std::atof(val.c_str()));
        break;
      }
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

}  // namespace cli
}  // namespace ra2
