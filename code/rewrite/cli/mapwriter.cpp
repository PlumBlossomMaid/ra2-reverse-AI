// mapwriter 实现 —— 见 mapwriter.h。
#include "mapwriter.h"

#include <cstdio>
#include <cstring>

#include "map/base64.h"
#include "map/encode80.h"
#include "map/isomap.h"

namespace ra2 {
namespace cli {

namespace {

// 等距 (ix, iy) → 真实 (rx, ry)。map_w = [0x87F8DC]（含 4 格余量）。
// 半整数问题见 mapwriter.h 头部注释（TODO(e2e)）。
void ToReal(int ix, int iy, int map_w, int* rx, int* ry) {
  *rx = (ix - iy + map_w - 1) / 2;
  *ry = (ix + iy - map_w - 1) / 2;
}

// EA 编辑器的键编码：value = "x" + "y补零3位"。y 的补零是把数字串
// 前补 '0' 到至少 3 位（strcpy_safe(j+1, j) 逐位前移）。
std::string KeyXY(int x, int y) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%d", y);
  std::string y3 = buf;
  while (y3.size() < 3) y3.insert(y3.begin(), '0');
  std::snprintf(buf, sizeof(buf), "%d", x);
  return std::string(buf) + y3;
}

void AppendB64Lines(std::string& out, const std::vector<std::uint8_t>& data) {
  const std::vector<std::string> lines = mapenc::UUBlockLines(data);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    char key[16];
    std::snprintf(key, sizeof(key), "%zu", i + 1);   // key = 行号，从 1 起
    out += key;
    out += '=';
    out += lines[i];
    out += '\n';
  }
}

}  // namespace

std::string BuildMapFile(const PipelineResult& r, int theater, int level) {
  std::string out;
  const int map_w = r.map_w + 4;    // [0x87F8DC]
  const int map_h = r.map_h + 12;   // [0x87F8E0]

  // ── [Map]（stage-init.md §三）──
  out += "[Map]\n";
  char buf[128];
  std::snprintf(buf, sizeof(buf), "Size=0,0,%d,%d\n", map_w, map_h);
  out += buf;
  std::snprintf(buf, sizeof(buf), "LocalSize=2,5,%d,%d\n", r.map_w, r.map_h);
  out += buf;
  out += "Theater=";
  out += (theater == 0) ? "TEMPERATE" : "SNOW";
  out += "\n";
  std::snprintf(buf, sizeof(buf), "Level=%d\n", level);
  out += buf;
  out += "\n";

  // ── [IsoMapPack5]（0x56B3F0 写链）──
  // slots 按 (y<<9)+x 排布，i 从 1 起、菱形外 NULL。
  std::vector<mapenc::IsoCell> pool;
  pool.reserve(0x10000);
  std::vector<const mapenc::IsoCell*> slots(0x40000, nullptr);
  for (int i = 1; i < 0x40000; ++i) {
    if (r.exists[static_cast<std::size_t>(i)] == 0) continue;
    const CliCell& c = r.cells[static_cast<std::size_t>(i)];
    mapenc::IsoCell rec;
    rec.x = static_cast<std::uint16_t>(i & 0x1FF);
    rec.y = static_cast<std::uint16_t>(i >> 9);
    rec.isoTile = static_cast<std::uint32_t>(c.isoTile) & 0xFFFF;  // 无效=-1 → 0xFFFF
    rec.height = static_cast<std::uint8_t>(c.subtile);   // +0x11A
    rec.level = static_cast<std::uint8_t>(c.level);      // +0x11B
    rec.unknown = 0;                                     // +0x119（未确认）
    pool.push_back(rec);
    slots[static_cast<std::size_t>(i)] = &pool.back();
  }
  const std::vector<std::uint8_t> isomap =
      mapenc::IsoMapPack5Encode(slots, 0x40000, map_w, map_h);
  out += "[IsoMapPack5]\n";
  AppendB64Lines(out, isomap);
  out += "\n";

  // ── [OverlayPack]（F80：512×512 表 → encode80 分 32 段）──
  // 游戏 OverlayClass 数组按 (y<<9)+x 索引；无 overlay 的格子是 0xFF。
  std::vector<std::uint8_t> overlay(0x40000, 0xFF);
  for (int i = 0; i < 0x40000; ++i) {
    if (r.exists[static_cast<std::size_t>(i)] == 0) continue;
    const int v = r.cells[static_cast<std::size_t>(i)].overlay;
    if (v != -1) overlay[static_cast<std::size_t>(i)] =
                     static_cast<std::uint8_t>(v);
  }
  const std::vector<std::uint8_t> ovl_pack =
      mapenc::EncodeF80(overlay, 32);
  out += "[OverlayPack]\n";
  AppendB64Lines(out, ovl_pack);
  out += "\n";

  // ── [Waypoints]（键 = 数字，值 = "x"+"y 补零 3 位"）──
  // 阶段 6 的 SetWaypoint(base+i) 从 base=0 起，waypoint 0 也有效；
  // 只写被填过的（坐标非 (0,0)）。
  bool has_wp = false;
  for (std::size_t i = 0; i < r.waypoints.size(); ++i) {
    if (r.waypoints[i].x != 0 || r.waypoints[i].y != 0) {
      has_wp = true;
      break;
    }
  }
  if (has_wp) {
    out += "[Waypoints]\n";
    for (std::size_t i = 0; i < r.waypoints.size(); ++i) {
      const rmg::CellCoord& c = r.waypoints[i];
      if (c.x == 0 && c.y == 0) continue;
      int rx, ry;
      ToReal(c.x, c.y, map_w, &rx, &ry);
      std::snprintf(buf, sizeof(buf), "%zu=", i);
      out += buf;
      out += KeyXY(rx, ry);
      out += "\n";
    }
    out += "\n";
  }

  // ── [Terrain]（键 = "x"+"y 补零 3 位"，值 = 地形名）──
  // 半整数取舍（ToReal 整数除法向零）会让相邻等距格映射到同一真实坐标，
  // 产生重复键；FA2 判定为 corrupt（修复时按键去重）。同键只写第一条。
  if (!r.terrains.empty()) {
    out += "[Terrain]\n";
    std::vector<bool> seen(0x40000, false);
    for (const TerrainRecord& t : r.terrains) {
      int rx, ry;
      ToReal(t.x, t.y, map_w, &rx, &ry);
      if (rx < 0 || ry < 0) continue;
      const std::size_t idx = static_cast<std::size_t>(ry) * 0x200 + rx;
      if (seen[idx]) continue;
      seen[idx] = true;
      out += KeyXY(rx, ry);
      out += '=';
      out += t.name;
      out += '\n';
    }
    out += "\n";
  }

  // ── [Structures]（17 字段逗号表，EA 编辑器顺序）──
  if (!r.buildings.empty()) {
    out += "[Structures]\n";
    for (std::size_t i = 0; i < r.buildings.size(); ++i) {
      const BuildingRecord& b = r.buildings[i];
      int rx, ry;
      ToReal(b.x, b.y, map_w, &rx, &ry);
      // owner,type,health,y,x,dir,tag,f1,f2,energy,upg,spot,u1,u2,u3,f3,f4
      std::snprintf(buf, sizeof(buf),
                    "%zu=%d,%s,%d,%d,%d,0,0,0,0,0,0,0,0,0,0,0,0\n", i + 1,
                    b.owner, b.type.c_str(), b.health, ry, rx);
      out += buf;
    }
    out += "\n";
  }

  return out;
}

}  // namespace cli
}  // namespace ra2
