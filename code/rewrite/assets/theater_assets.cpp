#include "theater_assets.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "zones.h"   // kTerrainToGround（0x8288E4）

namespace ra2 {
namespace assets {
namespace {

// 气候记录表 0x7E1B78 里本项目要用的四个字段。RMG 只有这两种气候。
struct TheaterNames {
  const char* prefix;      // +0x30
  const char* base_mix;    // +0x3A
  const char* md_mix;      // +0x44
  const char* ext;         // +0x4E
};
const TheaterNames kTheaters[2] = {
    {"TEMPERAT", "ISOTEMP", "ISOTEM", "TEM"},
    {"SNOW", "ISOSNOW", "ISOSNO", "SNO"},
};

bool ReadWholeFile(const std::string& path, std::vector<std::uint8_t>* out) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n <= 0) {
    std::fclose(f);
    return false;
  }
  out->assign(static_cast<std::size_t>(n), 0);
  const std::size_t got = std::fread(&(*out)[0], 1, out->size(), f);
  std::fclose(f);
  return got == out->size();
}

std::string Trim(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
  return s.substr(a, b - a);
}

bool IEquals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

std::string Lower(const std::string& s) {
  std::string r = s;
  for (std::size_t i = 0; i < r.size(); ++i) {
    r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(r[i])));
  }
  return r;
}

std::vector<rmg::FoundationOffset> RectFoundation(int w, int h) {
  std::vector<rmg::FoundationOffset> out;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) out.push_back({x, y});
  }
  out.push_back({0x7FFF, 0x7FFF});
  return out;
}

bool ParseFoundation(const std::string& s,
                     std::vector<rmg::FoundationOffset>* out) {
  const std::size_t x = s.find('x');
  if (x == std::string::npos) return false;
  const int w = std::atoi(s.substr(0, x).c_str());
  const int h = std::atoi(s.substr(x + 1).c_str());
  if (w <= 0 || h <= 0) return false;
  *out = RectFoundation(w, h);
  return true;
}

void ParseArtFoundations(const std::string& text,
                         std::map<std::string, std::vector<rmg::FoundationOffset>>* out) {
  out->clear();
  std::istringstream in(text);
  std::string line, section;
  while (std::getline(in, line)) {
    const std::size_t semi = line.find(';');
    if (semi != std::string::npos) line = line.substr(0, semi);
    line = Trim(line);
    if (line.empty()) continue;
    if (line.front() == '[') {
      const std::size_t r = line.find(']');
      section = (r == std::string::npos) ? line.substr(1) : line.substr(1, r - 1);
      continue;
    }
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos || section.empty()) continue;
    const std::string key = Trim(line.substr(0, eq));
    if (!IEquals(key, "Foundation")) continue;
    std::vector<rmg::FoundationOffset> fnd;
    if (ParseFoundation(Trim(line.substr(eq + 1)), &fnd)) {
      (*out)[section] = std::move(fnd);
    }
  }
}

}  // namespace

bool TheaterAssets::Load(const std::string& game_dir, Theater t,
                         std::string* err) {
  const TheaterNames& tn = kTheaters[t == kSnow ? 1 : 0];
  auto fail = [err](const char* m) {
    if (err != nullptr) *err = m;
    return false;
  };

  std::vector<std::uint8_t> raw;
  mix::MixArchive ra2, ra2md;
  if (!ReadWholeFile(game_dir + "/ra2md.mix", &raw)) {
    return fail("读不到 ra2md.mix");
  }
  if (!ra2md.Open(std::move(raw))) return fail("ra2md.mix 头解不开");
  if (!ReadWholeFile(game_dir + "/ra2.mix", &raw)) {
    return fail("读不到 ra2.mix");
  }
  if (!ra2.Open(std::move(raw))) return fail("ra2.mix 头解不开");

  // theater ini 在 ra2md.mix -> localmd.mix 里
  mix::MixArchive local;
  if (!local.OpenNested(ra2md, "localmd.mix")) return fail("找不到 localmd.mix");
  const std::string ini_name = Lower(std::string(tn.prefix) + "MD.INI");
  std::size_t sz = 0;
  const std::uint8_t* ini = local.Find(ini_name, &sz);
  if (ini == nullptr) return fail("找不到 theater ini");
  ini_text_.assign(reinterpret_cast<const char*>(ini), sz);
  if (!rmg::ParseTheaterIni(ini_text_, &data_)) return fail("theater ini 解析失败");

  std::vector<std::uint8_t> art_raw;
  if (local.Read("artmd.ini", &art_raw)) {
    const std::string art_text(art_raw.begin(), art_raw.end());
    ParseArtFoundations(art_text, &foundations_);
  } else {
    foundations_.clear();
  }

  // 砖库：扩展包一份 + 基础一份。
  // 两份的搜索优先级是**未确认**的（原版在 0x5349EA-0x534A3A 把五个 mix 名
  // 一起造好，谁先注册没查）。之所以不影响结果：实测两份归档几乎不重叠
  // ——温和 45 条对基础库只有 2 条同名、雪地 178 条同样只有 2 条，而那 2 条
  // 都不是 .tmp。也就是说 md 包只**新增**砖，不覆盖砖。
  std::vector<mix::MixArchive> arcs(2);
  const bool has_md =
      arcs[0].OpenNested(ra2md, Lower(std::string(tn.md_mix) + "MD.MIX"));
  const bool has_base =
      arcs[1].OpenNested(ra2, Lower(std::string(tn.base_mix) + ".MIX")) ||
      arcs[1].OpenNested(ra2md, Lower(std::string(tn.base_mix) + ".MIX"));
  if (!has_base && !has_md) return fail("找不到砖库 mix");

  const std::string ext = Lower(tn.ext);
  tiles_.assign(static_cast<std::size_t>(data_.total_tiles), TileType());
  for (std::size_t si = 0; si < data_.sets.size(); ++si) {
    const rmg::TileSetInfo& set = data_.sets[si];
    for (int k = 0; k < set.tiles_in_set; ++k) {
      char buf[256];
      std::snprintf(buf, sizeof(buf), "%s%02d.%s", set.file_name.c_str(), k + 1,
                    ext.c_str());
      TileType& tt = tiles_[static_cast<std::size_t>(set.start + k)];
      tt.file = Lower(buf);
      const std::uint8_t* blob = nullptr;
      std::size_t blob_sz = 0;
      for (int a = 0; a < 2; ++a) {
        if (a == 0 && !has_md) continue;
        if (a == 1 && !has_base) continue;
        blob = arcs[static_cast<std::size_t>(a)].Find(tt.file, &blob_sz);
        if (blob != nullptr) break;
      }
      if (blob == nullptr) continue;      // 0x546B93：BlocksX/Y 留 0
      mix::TmpTile parsed;
      if (!mix::ParseTmp(blob, blob_sz, &parsed)) continue;
      tt.loaded = true;
      tt.blocks_x = parsed.blocks_x;
      tt.blocks_y = parsed.blocks_y;
      tt.sub.swap(parsed.sub);
    }
  }
  return true;
}

void TheaterAssets::BuildRmgTileTable(std::vector<rmg::TileType>* out) const {
  out->assign(tiles_.size(), rmg::TileType());
  for (std::size_t i = 0; i < tiles_.size(); ++i) {
    const TileType& src = tiles_[i];
    rmg::TileType& dst = (*out)[i];
    dst.tile_number = static_cast<int>(i);      // +0x294
    dst.width = src.blocks_x;                   // +0x2E4
    dst.height = src.blocks_y;                  // +0x2E8
    dst.cells.assign(src.sub.size(), rmg::TileSubCell());
    for (std::size_t k = 0; k < src.sub.size(); ++k) {
      dst.cells[k].present = src.sub[k].present;
      dst.cells[k].height = src.sub[k].height;
      dst.cells[k].ramp = static_cast<int>(src.sub[k].ramp);
      dst.cells[k].terrain_type = static_cast<int>(src.sub[k].terrain);
    }
  }
}

int TheaterAssets::GroundTypeOf(int tile, int sub_index) const {
  if (tile < 0 || tile >= TileCount()) return 0;
  const TileType& tt = tiles_[static_cast<std::size_t>(tile)];
  if (tt.sub.empty()) return 0;              // 映像为空 -> 0x544BE0 返回 0
  const int n = static_cast<int>(tt.sub.size());
  int i = sub_index % n;                     // 原版就是取模
  if (i < 0) i += n;                         // C 的 % 对负数向零取整
  const std::size_t ui = static_cast<std::size_t>(i);
  if (!tt.sub[ui].present) return 0;         // 空洞
  const int terrain = tt.sub[ui].terrain;
  if (terrain < 0 || terrain >= 16) return 0;   // 真实数据里恒在 0..15
  return rmg::kTerrainToGround[terrain];
}

}  // namespace assets
}  // namespace ra2
