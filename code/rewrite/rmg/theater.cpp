#include "theater.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace ra2 {
namespace rmg {
namespace {

std::string Lower(const std::string& s) {
  std::string r = s;
  for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return std::string();
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

using Section = std::map<std::string, std::string>;

// 够用的 ini 解析：只需要 [General] 与 [TileSetNNNN] 两类节。
// 注释以 ';' 起头（与原版 INIClass 一致），键名统一小写。
std::map<std::string, Section> ParseIni(const std::string& text) {
  std::map<std::string, Section> out;
  std::string cur;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    std::string line = text.substr(pos, eol - pos);
    pos = eol + 1;
    size_t semi = line.find(';');
    if (semi != std::string::npos) line = line.substr(0, semi);
    line = Trim(line);
    if (line.empty()) continue;
    if (line.front() == '[') {
      size_t close = line.find(']');
      if (close != std::string::npos) {
        cur = line.substr(1, close - 1);
        out[cur];
      }
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string::npos || cur.empty()) continue;
    out[cur][Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
  }
  return out;
}

int GetInt(const Section& s, const char* key, int fallback) {
  auto it = s.find(key);
  if (it == s.end()) return fallback;
  return std::atoi(it->second.c_str());
}

std::string GetStr(const Section& s, const char* key) {
  auto it = s.find(key);
  return it == s.end() ? std::string() : it->second;
}

// "TileSet0021" -> 21；不是这个形状就返回 -1。
int TileSetIndex(const std::string& name) {
  if (name.size() != 11) return -1;
  if (Lower(name.substr(0, 7)) != "tileset") return -1;
  for (size_t i = 7; i < 11; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(name[i]))) return -1;
  }
  return std::atoi(name.c_str() + 7);
}

}  // namespace

int TheaterData::TilesInSetStartingAt(int start_tile) const {
  for (const TileSetInfo& s : sets) {
    if (s.start == start_tile) return s.tiles_in_set;
  }
  return 0;
}

bool ParseTheaterIni(const std::string& text, TheaterData* out) {
  const std::map<std::string, Section> ini = ParseIni(text);

  // 起始砖号是一条前缀和（0x545C6B 的 ebx 累加），按组序号升序累。
  std::vector<int> ids;
  for (const auto& kv : ini) {
    const int i = TileSetIndex(kv.first);
    if (i >= 0) ids.push_back(i);
  }
  if (ids.empty()) return false;
  std::sort(ids.begin(), ids.end());

  out->sets.clear();
  int acc = 0;
  for (int i : ids) {
    char name[16];
    std::snprintf(name, sizeof(name), "TileSet%04d", i);
    const Section& s = ini.at(name);
    TileSetInfo info;
    info.index = i;
    info.tiles_in_set = GetInt(s, "tilesinset", 0);
    info.start = acc;
    info.set_name = GetStr(s, "setname");
    info.file_name = GetStr(s, "filename");
    acc += info.tiles_in_set;
    out->sets.push_back(info);
  }
  out->total_tiles = acc;

  // 组序号 -> 起始砖号。查不到键（该 theater 没定义）就留 -1，
  // 与原版「ReadInteger 默认值 -1、枚举时没有组能命中 -1」的结果一致。
  auto gi = ini.find("General");
  const Section empty;
  const Section& gen = gi == ini.end() ? empty : gi->second;
  auto start_of = [&](const char* key) -> int {
    const int idx = GetInt(gen, key, -1);
    if (idx < 0) return -1;
    for (const TileSetInfo& s : out->sets) {
      if (s.index == idx) return s.start;
    }
    return -1;
  };

  out->water_set = start_of("waterset");
  out->shore_pieces = start_of("shorepieces");
  out->green_tile = start_of("greentile");
  out->clear_to_green_lat = start_of("cleartogreenlat");
  out->clear_tile = start_of("cleartile");
  out->rough_tile = start_of("roughtile");
  out->sand_tile = start_of("sandtile");
  out->clear_to_sand_lat = start_of("cleartosandlat");
  out->waterfall_east = start_of("waterfalleast");
  out->waterfall_west = start_of("waterfallwest");
  out->waterfall_north = start_of("waterfallnorth");
  out->waterfall_south = start_of("waterfallsouth");
  out->water_cliffs = start_of("watercliffs");
  out->ramp_base = start_of("rampbase");
  out->bridge_set = start_of("bridgeset");
  out->black_tile = start_of("blacktile");
  return true;
}

}  // namespace rmg
}  // namespace ra2
