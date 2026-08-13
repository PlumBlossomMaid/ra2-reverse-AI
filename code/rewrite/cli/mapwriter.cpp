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

// EA 编辑器的键编码：value = "x" + "y补零3位"。y 的补零是把数字串
// 前补 '0' 到至少 3 位（strcpy_safe(j+1, j) 逐位前移）。
// 注意：x/y 是**等距坐标**（cell 列/行），不是真实坐标——EA 官方图
// （sandwell 100×150 waypoint (229,148)、捡箱子 200×200 waypoint (374,203)）
// 都远超地图尺寸，实锤等距语义（FA2 PosToXY 后按 x + y*IsoSize 做 cell 索引）。
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

  // ── [Basic]（FA2 IsMultiplayer 判定，MapData.cpp:6085）──
  // Player 键 = 单人剧情图标志：有它 FA2 走单人渲染路径，无 [Houses]
  // 时野指针崩溃（EA 官方多人图 sandwell.yrm 无 Player、MultiplayerOnly=1）。
  // 多人遭遇战图必须 MultiplayerOnly=1。其余键值以 e2e 对拍原版
  // RandMap.Map 为准。
  out += "[Basic]\n";
  out += "Name=Random Map\n";
  out += "MultiplayerOnly=1\n";
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

  // ── [OverlayDataPack]（F80：512×512 矿石量表 → encode80 分 32 段）──
  // 与 OverlayPack 同构，只是内容换成 +0x11E 的矿石量（无矿格 0）。
  // 缺这个节游戏里矿车看不到矿（矿类型在、矿量为空）。
  std::vector<std::uint8_t> ovldata(0x40000, 0);
  for (int i = 0; i < 0x40000; ++i) {
    if (r.exists[static_cast<std::size_t>(i)] == 0) continue;
    ovldata[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>(r.cells[static_cast<std::size_t>(i)].overlayData);
  }
  const std::vector<std::uint8_t> ovld_pack =
      mapenc::EncodeF80(ovldata, 32);
  out += "[OverlayDataPack]\n";
  AppendB64Lines(out, ovld_pack);
  out += "\n";

  // ── [Waypoints]（键 = 数字，值 = "x"+"y 补零 3 位"，等距坐标）──
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
      std::snprintf(buf, sizeof(buf), "%zu=", i);
      out += buf;
      out += KeyXY(c.x, c.y);
      out += "\n";
    }
    out += "\n";
  }

  // ── [Terrain]（键 = "x"+"y 补零 3 位"，值 = 地形名，等距坐标）──
  if (!r.terrains.empty()) {
    out += "[Terrain]\n";
    for (const TerrainRecord& t : r.terrains) {
      out += KeyXY(t.x, t.y);
      out += '=';
      out += t.name;
      out += '\n';
    }
    out += "\n";
  }

  // ── [Structures]（17 字段逗号表，EA 编辑器顺序，y/x 等距坐标）──
  if (!r.buildings.empty()) {
    out += "[Structures]\n";
    for (std::size_t i = 0; i < r.buildings.size(); ++i) {
      const BuildingRecord& b = r.buildings[i];
      // owner,type,health,y,x,dir,tag,f1,f2,energy,upg,spot,u1,u2,u3,f3,f4
      std::snprintf(buf, sizeof(buf),
                    "%zu=%d,%s,%d,%d,%d,0,0,0,0,0,0,0,0,0,0,0,0\n", i + 1,
                    b.owner, b.type.c_str(), b.health, b.y, b.x);
      out += buf;
    }
    out += "\n";
  }

  return out;
}

}  // namespace cli
}  // namespace ra2
