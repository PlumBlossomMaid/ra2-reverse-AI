// 从原版游戏目录装载一个气候（theater）的全部砖块几何。
//
// 这一层是「算法核心」与「游戏数据」之间的唯一接口：rmg/ 里的算法只吃已经算好的
// 数值，装载的脏活全在这里。仓库里不放任何游戏素材，运行时从 .mix 现读。
//
// 链条（取证见 docs/random-map/theater-data.md 与 docs/mix-format/README.md）：
//   1. 气候记录表 0x7E1B78，每条 0x70 字节：
//        +0x00 名字 TEMPERATE          +0x10 UI 串 Name:Temperate
//        +0x30 ini/mix 前缀 TEMPERAT   -> %sMD.INI = TEMPERATMD.INI
//        +0x3A 基础砖库名 ISOTEMP      -> %s.MIX   = ISOTEMP.MIX
//        +0x44 扩展包砖库名 ISOTEM     -> %sMD.MIX = ISOTEMMD.MIX
//        +0x4E 砖文件扩展名 TEM
//      五个名字的拼装点在 0x5349EA-0x534A3A（都走同一个 sprintf 函数指针
//      [0x7E14B0]）。RMG 只开放 temperate 与 snow 两种气候。
//   2. TEMPERATMD.INI / SNOWMD.INI 在 ra2md.mix -> localmd.mix 里。
//   3. 砖号 = 各组 TilesInSet 的前缀和；组内第 k 块的文件名是
//      FileName + 两位序号(k+1) + 扩展名。
//
// 缺文件是常态：温和有 343 块砖（20 个组）根本没有 .tmp，全是
// ZMM（Marble Madness 编辑器视图）与 Obsolete 之类。原版对此的处理在
// 0x546B8B：第一次装载就没拿到映像时把 BlocksX/BlocksY 写 0
// （mov dword ptr [ebp+0x2E4], 0），而 0x544BE0 取地面类别时映像为空直接返回 0。
// 这里照抄。
#ifndef RA2_ASSETS_THEATER_ASSETS_H_
#define RA2_ASSETS_THEATER_ASSETS_H_

#include <string>
#include <vector>

#include "mix_archive.h"
#include "theater.h"
#include "tmp_file.h"

namespace ra2 {
namespace assets {

struct TileType {
  std::string file;                    // 小写文件名，便于诊断
  bool loaded = false;                 // 砖库里有没有这个 .tmp
  int blocks_x = 0;                    // TileTypeClass +0x2E4（缺文件时 0）
  int blocks_y = 0;                    // +0x2E8
  std::vector<mix::TmpSubTile> sub;    // blocks_x * blocks_y 项；缺文件时空
};

class TheaterAssets {
 public:
  enum Theater { kTemperate = 0, kSnow = 1 };

  // game_dir 是原版目录（含 ra2.mix / ra2md.mix）。失败时 err 写原因。
  bool Load(const std::string& game_dir, Theater t, std::string* err);

  const rmg::TheaterData& data() const { return data_; }
  const std::string& ini_text() const { return ini_text_; }
  int TileCount() const { return static_cast<int>(tiles_.size()); }
  const TileType& Tile(int n) const {
    return tiles_[static_cast<std::size_t>(n)];
  }

  // 0x544BE0 TileTypeClass::GetGroundType(subIndex)。
  // 子格下标按 BlocksX*BlocksY 取模（param_2 % (piVar1[1] * *piVar1)），
  // 越界不会崩、会绕回去；空洞与缺文件都返回 0。
  int GroundTypeOf(int tile, int sub_index) const;

 private:
  rmg::TheaterData data_;
  std::string ini_text_;
  std::vector<TileType> tiles_;
};

}  // namespace assets
}  // namespace ra2

#endif  // RA2_ASSETS_THEATER_ASSETS_H_
