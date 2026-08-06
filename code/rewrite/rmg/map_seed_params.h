// MapSeedClass 的参数集：RandMap.Sed 的读取与默认值
//
// 字段偏移、键名、默认值全部取自 gamemd.exe：
//   构造 MapSeedClass::MapSeedClass  0x595680（默认值）
//   读盘 MapSeedClass::LoadMission   0x597A30（16 个 ReadInteger + 1 个 ReadString）
// 详见 docs/random-map/parameters.md。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ra2::rmg {

// 对应 MapSeedClass +0x38 .. +0x178。注释里的偏移是原版对象内的偏移，
// 复刻时不复制内存布局，只保证语义一致。
struct MapSeedParams {
  int theater = 0;          // +0x38  RMG 只开放 0=温和 / 1=雪地
  int map_type = 1;         // +0x3C
  int resources = 1;        // +0x40
  int ruggedness = 0;       // +0x44
  int time = 1;             // +0x48
  int water_amount = 0;     // +0x4C
  int num_players = 2;      // +0x50
  int tiberium = 0;         // +0x54
  int tiberium_layout = 0;  // +0x58
  int vegetation = 0;       // +0x5C
  int urban_presence = 0;   // +0x60
  int width = 0;            // +0x64  档位 0..3，不是格数
  int height = 0;           // +0x68  档位 0..3，不是格数
  int accessibility = 0;    // +0x6C
  int region_size = 0;      // +0x70
  int seed = -1;            // +0x74
  std::u16string description;  // +0x78  FixedWString<0x80>
};

// 西木家 INIClass::Get_Int 的取值语义（0x5276D0 尾段实锤，与 RA1 INI.CPP 一致）：
//   '$' 开头        -> sscanf("$%x")
//   末字符是 h/H    -> sscanf("%xh")
//   其余            -> atoi（前导空白 + 可选符号 + 十进制，遇到非法字符即停）
// 取不到键时返回 fallback（原版把「字段当前值」当默认值传进去）。
int IniGetInt(const std::string& text, int fallback);

// Description 是 UTF-16 码位的十六进制逗号列表，如 "968f,673a,5730,56fe,"
std::u16string DecodeDescription(const std::string& text);
std::string EncodeDescription(const std::u16string& s);

// 解析 RandMap.Sed 的内容（[RandomMap] 节）。缺失的键保留 out 里的原值——
// 与原版 LoadMission 把当前字段值当默认值传给 ReadInteger 的行为一致。
// 返回 false 表示连 [RandomMap] 节都没有。
bool ParseSed(const std::string& content, MapSeedParams* out);
bool LoadSedFile(const std::string& path, MapSeedParams* out);

}  // namespace ra2::rmg
