// theater（气候）数据：TileSet 枚举 -> 各"语义组"的起始砖号
//
// 随机地图生成器判水、铺岸、涂草全靠比对 `CellClass +0x38`（IsoTileTypeIndex）
// 与一堆 .bss 全局。那些全局的值不在 exe 里，是装载 theater 时从
// TEMPERATMD.INI / SNOWMD.INI 算出来的。
//
// 取证（gamemd.exe）：
//   theater 装载 0x545150；ini 键名读进栈槽 0x545570-0x545B..；
//   TileSet 枚举与回填 0x545C6B-0x545FA3（ebx 累加 TilesInSet，
//   每轮开头即当前组起始砖号）；ini 文件名模板 "%sMD.INI" @0x8295E8
// 全局↔键名对照见 memory/data/rmg/tileset_globals.txt，
// 算出来的起始砖号见 memory/data/rmg/tileset_starts.txt。
//
// ⚠️ 只有温和与雪地两种气候会出现在 RMG 里（游戏内实测下拉框就这两项）。
#ifndef RA2_RMG_THEATER_H_
#define RA2_RMG_THEATER_H_

#include <map>
#include <string>
#include <vector>

namespace ra2 {
namespace rmg {

// theater ini 里的一个 TileSet 节。
struct TileSetInfo {
  int index = 0;          // TileSetNNNN 的 NNNN
  int tiles_in_set = 0;   // TilesInSet
  int start = 0;          // 起始砖号 = 前面所有组的 TilesInSet 之和
  std::string set_name;
  std::string file_name;
};

// 装载完 theater 之后那些全局的取值。字段名沿用 ini 键名。
struct TheaterData {
  std::vector<TileSetInfo> sets;
  int total_tiles = 0;

  // 随机地图生成器真正会读的那几个（括号里是对应的 .bss 全局）
  int water_set = -1;         // 0xAA0738  判水用；组内 14 块
  int shore_pieces = -1;      // 0xABAD28  岸砖；组内 42 块
  int green_tile = -1;        // 0xAA0E18  IsGreen 主判据
  int clear_to_green_lat = -1;  // 0xAA0748  IsGreen 的过渡砖（16 块）
  int clear_tile = -1;        // 0xAA10B0
  int rough_tile = -1;        // 0xABC2B8
  int sand_tile = -1;         // 0xABB104
  int clear_to_sand_lat = -1; // 0xAA0E24
  int waterfall_east = -1;    // 0xAA073C
  int waterfall_west = -1;    // 0xABB110
  int waterfall_north = -1;   // 0xAA10A0
  int waterfall_south = -1;   // 0xAA1050
  int water_cliffs = -1;      // 0xAA101C
  int ramp_base = -1;         // 0xABC1D8
  int bridge_set = -1;        // 0xAA0E28
  int black_tile = -1;        // 0xABC2CC
  int cliff_set = -1;         // 0xAA1020  贴砖器的三个"地形不可覆盖"谓词要读
  int cliff_ramps = -1;       // 0xABBEBC
  int ramp_smooth = -1;       // 0xAA1058
  int water_caves = -1;       // 0xABAD24
  int wood_bridge_set = -1;   // 0xABAD1C
  int destroyable_cliffs = -1;  // 0xABC2C8
  // 只有 0x5A4B60 的 allowNonPlain 死分支读这两个（0x486670 / 0x486650）。
  // 温和气候里两者都是 -1（ini 没这两个键），雪地同理。
  int pave_tile = -1;         // 0xABC2B0，宽 0x10
  int misc_pave_tile = -1;    // 0xAA10A4，宽 0x0E
  // 阶段 6 的 0x5A7250 还要读这两个（0x4866D0 / 0x4866F0）。
  // ⚠️ 0x4866D0 写死的宽度是 0x0F，而 PavedRoads 的 TilesInSet 是 21——
  // 原版就是短的，照抄。
  int paved_roads = -1;       // 0xABBEC8，代码写死宽 0x0F
  int paved_road_ends = -1;   // 0xABBEC4，宽 4

  // 组内砖数；用于 IsWater 那种「起始砖号 + 宽度」的区间判定。
  int TilesInSetStartingAt(int start_tile) const;
};

// 解析 theater ini 的文本。原版 INIClass 的取值语义见 map_seed_params.h；
// 这里只需要整数与字符串两种，键名大小写不敏感。
// 返回 false 表示连一个 [TileSetNNNN] 都没有。
bool ParseTheaterIni(const std::string& text, TheaterData* out);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_THEATER_H_
