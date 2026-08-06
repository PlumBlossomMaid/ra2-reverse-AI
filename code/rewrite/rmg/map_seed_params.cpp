#include "map_seed_params.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ra2::rmg {
namespace {

std::string Trim(const std::string& s) {
  std::size_t a = 0, b = s.size();
  auto ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
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

}  // namespace

int IniGetInt(const std::string& text, int fallback) {
  if (text.empty()) return fallback;
  unsigned value = 0;
  if (text[0] == '$') {
    if (std::sscanf(text.c_str(), "$%x", &value) == 1)
      return static_cast<int>(value);
    return fallback;
  }
  const char last = text.back();
  if (last == 'h' || last == 'H') {
    if (std::sscanf(text.c_str(), "%xh", &value) == 1)
      return static_cast<int>(value);
    return fallback;
  }
  return std::atoi(text.c_str());
}

std::u16string DecodeDescription(const std::string& text) {
  std::u16string out;
  std::size_t i = 0;
  while (i < text.size()) {
    std::size_t j = text.find(',', i);
    if (j == std::string::npos) j = text.size();
    std::string tok = Trim(text.substr(i, j - i));
    if (!tok.empty()) {
      unsigned v = 0;
      if (std::sscanf(tok.c_str(), "%x", &v) == 1)
        out.push_back(static_cast<char16_t>(v));
    }
    i = j + 1;
  }
  return out;
}

std::string EncodeDescription(const std::u16string& s) {
  std::string out;
  char buf[8];
  for (char16_t c : s) {
    std::snprintf(buf, sizeof(buf), "%04x,", static_cast<unsigned>(c));
    out += buf;
  }
  return out;
}

bool ParseSed(const std::string& content, MapSeedParams* out) {
  std::istringstream in(content);
  std::string line, section;
  bool seen = false;
  while (std::getline(in, line)) {
    // 原版 INIClass 以 ';' 起注释；键值行里的 ';' 不在本文件格式中出现
    std::size_t semi = line.find(';');
    if (semi != std::string::npos) line = line.substr(0, semi);
    line = Trim(line);
    if (line.empty()) continue;
    if (line.front() == '[') {
      std::size_t r = line.find(']');
      section = (r == std::string::npos) ? line.substr(1) : line.substr(1, r - 1);
      if (IEquals(section, "RandomMap")) seen = true;
      continue;
    }
    if (!IEquals(section, "RandomMap")) continue;
    std::size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = Trim(line.substr(0, eq));
    const std::string val = Trim(line.substr(eq + 1));

    // 顺序照抄 LoadMission 0x597A30 的 16 次 ReadInteger
    if (IEquals(key, "Width")) out->width = IniGetInt(val, out->width);
    else if (IEquals(key, "Height")) out->height = IniGetInt(val, out->height);
    else if (IEquals(key, "NumPlayers")) out->num_players = IniGetInt(val, out->num_players);
    else if (IEquals(key, "Seed")) out->seed = IniGetInt(val, out->seed);
    else if (IEquals(key, "MapType")) out->map_type = IniGetInt(val, out->map_type);
    else if (IEquals(key, "Theater")) out->theater = IniGetInt(val, out->theater);
    else if (IEquals(key, "Time")) out->time = IniGetInt(val, out->time);
    else if (IEquals(key, "RegionSize")) out->region_size = IniGetInt(val, out->region_size);
    else if (IEquals(key, "Ruggedness")) out->ruggedness = IniGetInt(val, out->ruggedness);
    else if (IEquals(key, "Accessibility")) out->accessibility = IniGetInt(val, out->accessibility);
    else if (IEquals(key, "WaterAmount")) out->water_amount = IniGetInt(val, out->water_amount);
    else if (IEquals(key, "Tiberium")) out->tiberium = IniGetInt(val, out->tiberium);
    else if (IEquals(key, "TiberiumLayout")) out->tiberium_layout = IniGetInt(val, out->tiberium_layout);
    else if (IEquals(key, "Vegetation")) out->vegetation = IniGetInt(val, out->vegetation);
    else if (IEquals(key, "UrbanPresence")) out->urban_presence = IniGetInt(val, out->urban_presence);
    else if (IEquals(key, "Resources")) out->resources = IniGetInt(val, out->resources);
    else if (IEquals(key, "Description")) out->description = DecodeDescription(val);
  }
  return seen;
}

bool LoadSedFile(const std::string& path, MapSeedParams* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  return ParseSed(ss.str(), out);
}

}  // namespace ra2::rmg
