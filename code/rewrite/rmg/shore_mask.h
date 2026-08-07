// 八邻域「水」掩码 `FUN_0057B210`——岸线平滑四遍扫描共用的核心工具。
//
// 一个 bit = 一个邻格是不是水砖。四遍平滑（侵蚀 / 清理 / 直边 / 拐角）
// 全部建立在这个掩码之上，所以它错一位，整条岸线就走样。
//
// 取证（gamemd.exe）：0x57B210-0x57B433，判水谓词 0x485060，
// 未定砖谓词 0x486380。详见 docs/random-map/stage-seeding-water.md §七。
#ifndef RA2_RMG_SHORE_MASK_H_
#define RA2_RMG_SHORE_MASK_H_

#include "map_grid.h"

namespace ra2 {
namespace rmg {

// 掩码的位定义（0x57B321 起逐条 `or edi, imm`）：
//   bit k ⟺ AdjacentCell[(k + 1) & 7] 是水砖
// 即 bit0=NE bit1=E bit2=SE bit3=S bit4=SW bit5=W bit6=NW bit7=N。
enum ShoreMaskBit {
  kMaskNE = 0x01,
  kMaskE  = 0x02,
  kMaskSE = 0x04,
  kMaskS  = 0x08,
  kMaskSW = 0x10,
  kMaskW  = 0x20,
  kMaskNW = 0x40,
  kMaskN  = 0x80,
};

// 掩码只需要读瓦片号和一个「有效位」，所以依赖面比 PaintWorld 还小。
class ShoreWorld {
 public:
  virtual ~ShoreWorld() = default;

  // CellClass +0x38 = IsoTileTypeIndex；格子不存在时返回 kNoCell
  static constexpr int kNoCell = -0x7FFFFFFF;
  virtual int IsoTile(const CellCoord& c) const = 0;

  // [0xAA0738] 水面组基下标；判水区间是 [base, base + 0x0E)
  virtual int WaterTile() const = 0;

  // 工作记录 +0x4A「这条记录有效」
  virtual bool WorkValid(const CellCoord& c) const = 0;

  // 工作记录 +0x40 掩码缓存，-1（任意负值）= 失效
  virtual int MaskCache(const CellCoord& c) const = 0;
};

// 0x485060 IsWater：tile ∈ [WaterTile, WaterTile + 0x0E)
inline bool IsWaterTile(int tile, int water_base) {
  return tile >= water_base && tile < water_base + 0x0E;
}

// 0x486380：这一格还没定砖（0 或 0xFFFF）
inline bool IsUndecided(int tile) { return tile == 0 || tile == 0xFFFF; }

// mode 决定的前置门槛（0x57B27F-0x57B2B9）：
//   0 = 中心格必须「尚未定砖」，否则返回 0
//   1 = 中心格自己是水砖则返回 0
//   2 = 无门槛
int ShoreMask(const ShoreWorld& world, const MapGrid& grid, const CellCoord& c,
              int mode);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_SHORE_MASK_H_
