// .tmp 等距砖文件的**几何/属性**解析（不碰像素，也不碰调色板）。
//
// 随机地图生成器要的只有四样：砖占几格、每个子格在不在、子格的高度增量、
// 地形类别、坡道类别。像素数据一概不读。
//
// 文件头（由装载循环 0x546B10-0x546B84 实锤）：
//   dword[0] BlocksX -> TileTypeClass +0x2E4，原版 `mov al,[ebx]` **只取低字节**
//   dword[1] BlocksY -> +0x2E8，同样只取低字节
//   dword[2] 单块像素宽（实测恒 60）
//   dword[3] 单块像素高（实测恒 30）
//   dword[4..] 子块偏移表，共 BlocksX*BlocksY 项；**0 = 空洞**
// 子块头里本项目要的三个字节：
//   +0x28 Height       贴砖器 0x57B440 的逐格高度增量
//   +0x29 TerrainType  0x544BE0 拿它查 TerrainToGround（0x8288E4）
//   +0x2A RampType     压砖器 0x5A6C10 写进 CellClass +0x11C
#ifndef RA2_MIX_TMP_FILE_H_
#define RA2_MIX_TMP_FILE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ra2 {
namespace mix {

struct TmpSubTile {
  bool present = false;
  std::uint8_t height = 0;
  std::uint8_t ramp = 0;
  std::int8_t terrain = 0;
};

struct TmpTile {
  int blocks_x = 0;    // 已按原版取低字节
  int blocks_y = 0;
  int pixel_w = 0;
  int pixel_h = 0;
  std::vector<TmpSubTile> sub;   // blocks_x * blocks_y 项，按 y * blocks_x + x

  const TmpSubTile& At(int x, int y) const {
    return sub[static_cast<std::size_t>(y) * blocks_x + x];
  }
};

// 解析失败（长度不够 / 偏移越界）返回 false。
bool ParseTmp(const std::uint8_t* data, std::size_t size, TmpTile* out);

}  // namespace mix
}  // namespace ra2

#endif  // RA2_MIX_TMP_FILE_H_
