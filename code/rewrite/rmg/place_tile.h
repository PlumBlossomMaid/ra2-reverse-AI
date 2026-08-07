// 贴砖器 `FUN_0057B440`：把一块多格砖压到地图上，逐格校验能不能压。
//
// 岸线平滑的第 3/4 遍（`0x57ACF0`）挑好一块岸砖之后，就靠它落地。
// 它是"先全格试探、再全格写入"的两段式：任何一格不合格就整块放弃，
// 并通过出参把调用方的 `ok` 置 false —— 那会让整遍岸线中断、整块陆地回滚。
//
// 取证（gamemd.exe）：主体 0x57B440-0x57B78D，
//   调用点 0x57B1E3（第 3 遍传 (0,0)，第 4 遍传 (ShorePieces, ShorePieces+0x29)），
//   工作数组存取 0x5A00C0 / 0x5A0090，
//   谓词 0x486380 未定砖 / 0x4865B0 是岸砖 / 0x4863D0 与 0x578D80 地形保护，
//   两张 42 项静态属性表 0x82A7F4（族）与 0x82A89C（朝向）。
// 详见 docs/random-map/stage-seeding-water.md §七。
#ifndef RA2_RMG_PLACE_TILE_H_
#define RA2_RMG_PLACE_TILE_H_

#include <vector>

#include "map_grid.h"
#include "theater.h"

namespace ra2 {
namespace rmg {

// 砖块本身（`TileTypeClass`）。贴砖器只读这三样：
//   +0x294 砖号（这块砖在 theater 全局砖表里的起始序号）
//   +0x2E4 / +0x2E8 占地宽高，来自 .tmp 文件头的 byte[0]/byte[4]
//   每个子格的 +0x28 高度增量（.tmp 子块头的 byte[0x28]）
// 子格按 `y * width + x` 排列，空洞（.tmp 偏移表里为 0）用 present=false 表示。
struct TileSubCell {
  bool present = false;   // .tmp 偏移表该项为 0 时是空洞，原版跳过不贴
  int height = 0;         // .tmp 子块头 +0x28
  // .tmp 子块头 +0x2A（RampType）。贴砖器 0x57B440 **不读它**，
  // 只有另一个压砖器 0x5A6C10 会把它写进 CellClass +0x11C。
  int ramp = 0;
};

struct TileType {
  int tile_number = 0;    // +0x294
  int width = 0;          // +0x2E4
  int height = 0;         // +0x2E8
  std::vector<TileSubCell> cells;   // width * height 条

  const TileSubCell& At(int x, int y) const { return cells[y * width + x]; }
};

// 贴砖器要碰的格子状态。字段与 CellClass 的偏移一一对应。
//
// 「哑元格子」：0x57B537 在拿不到真实格子指针时会退到静态对象 0xABDC50，
// 并把坐标写进它的 +0x24。它是全局共享的一份，上一次贴砖留下的 +0x38
// 会被下一次读到。这是原版行为，实现方必须照样提供一份可写的哑元。
class TileWorld {
 public:
  virtual ~TileWorld() = default;

  // 格子指针表 [0x87F924] 里这一格是不是非空
  virtual bool CellExists(const CellCoord& c) const = 0;

  virtual int IsoTile(const CellCoord& c) const = 0;        // +0x38
  virtual int SubTile(const CellCoord& c) const = 0;        // +0x11A
  virtual void SetIsoTile(const CellCoord& c, int v) = 0;
  virtual void SetSubTile(const CellCoord& c, int v) = 0;
  virtual void SetLevel(const CellCoord& c, int v) = 0;     // +0x11B
  // +0x11C，只有 StampTile（0x5A6C10）写；PlaceTile（0x57B440）从不碰
  virtual void SetRamp(const CellCoord& c, int v) = 0;

  // 共享哑元格子 0xABDC50
  virtual void SetDummyCoord(const CellCoord& c) = 0;       // 写 0xABDC74
  virtual int DummyIsoTile() const = 0;
  virtual int DummySubTile() const = 0;
  virtual void SetDummyTile(int tile, int subtile, int level) = 0;
  // 阶段 2 收尾的刷绿（0x59A8C0）只写哑元的 +0x38，不碰 +0x11A/+0x11B。
  // 与上面那个三写版本语义不同，不能互相代替。
  virtual void SetDummyIsoTile(int tile) = 0;

  // 工作数组 +0x38（0x5A00C0 读 / 0x5A0090 写）——这一格属于哪个块
  virtual int WorkBlob(const CellCoord& c) const = 0;
  virtual void SetWorkBlob(const CellCoord& c, int v) = 0;
};

// 六个栈参，逐个对应 0x57B440 的栈帧（ret 0x18）。
// 偏移按序言之后算：`sub esp,0x1c` + 4 个 push = 0x2C，故第一个参数在 [esp+0x30]。
struct PlaceTileParams {
  int allow_lo = 0;       // [esp+0x30] 允许覆盖的砖号区间下界
  int allow_hi = 0;       // [esp+0x34] 上界（闭区间，见 0x57B63E/0x57B646）
  int base_level = 0;     // [esp+0x38] 落点基准高度，逐子格再加砖的高度增量
  int blob_id = 0;        // [esp+0x3C] 归属块编号；0 与 -1 是两个特例值
  bool* ok = nullptr;     // [esp+0x40] 出参：贴不上时被写 false
  // [esp+0x44] 非零 = 把 blob_id 强行清零（0x57B45B/0x57B463），于是工作数组
  // 既不读也不写，判定恒走"同块"分支。
  // ⚠️ 岸线第 3/4 遍传进来的是 Shoreline 自己的 flag（0x57B1A4 与 0x57B1BE 各
  // 把栈上的 flag 作为第一个 push）。全镜像 5 个 Shoreline 调用点里 flag 有
  // 1、0 和变量三种，别假定它恒为某个值：涂块收尾（0x59C433）是 1，
  // 阶段 2 调度收尾（0x59A788）是 0。
  bool no_blob_tracking = false;
};

// 落点由 [0x88095C]（真实坐标 X/Y 两个 int16）给出，这里显式传入。
// 返回 false = 整块贴不上；此时 *ok 可能已被置 false（两者语义不同，见文档）。
bool PlaceTile(TileWorld& world, const MapGrid& grid, const TheaterData& theater,
               const TileType& tile, const CellCoord& at,
               const PlaceTileParams& params);

// ── 另一个压砖器 `FUN_005A6C10`（`__fastcall`，`ecx` = 砖下标、`edx` = 落点，
//    两个栈参 `ret 8`）。全镜像 49 处调用点，RMG 主路径上只有水面细化
//    0x59C795 一处（其余在 0x58F/0x593 与 0x5A5 两棵死代码子树里）。
//
// 与上面的 `PlaceTile` 是**两套东西**，别混：
//   · 它一格校验都不做——不看归属、不看 allow 区间、不看地形保护，压上就是压上
//   · 它多写一个字段 `CellClass +0x11C = 子块的 RampType`（0x5A6CFC-0x5A6D12）
//   · 层高的公式带一个 **-4**：`+0x11B = 子块高度 + level - 4`（0x5A6CFF/0x5A6D01），
//     而 `PlaceTile` 是 `子块高度 + base_level`，没有 -4
//   · `level == -1` 时整条层高写入被跳过（0x5A6CF7 的 `cmp ecx,-1`），
//     但 `+0x11C` **照写不误**
//   · 工作数组的归属是**无条件**写 `blob_id`（0x5A6D3A），没有"抢占"判定
// 逐子格的菱形边界判定与 `MapGrid::InMap` 四条不等式逐条相同（0x5A6C90-0x5A6CB4）。
void StampTile(TileWorld& world, const MapGrid& grid, const TileType& tile,
               const CellCoord& at, int blob_id, int level);

// 0x82A7F4 / 0x82A89C：两张写死在 exe 里的 int[42]，索引 = 砖号 - ShorePieces。
// 越界即判失败（0x57B5A3-0x57B5B7 的四条比较）。
extern const int kShoreFamily[42];      // 0x82A7F4
extern const int kShoreFacing[42];      // 0x82A89C

// 0x4865B0：砖号落在岸砖组内（宽度 0x2A 是写死的，实测等于 ShorePieces 的 TilesInSet）
inline bool IsShoreTile(int tile, int shore_base) {
  return tile >= shore_base && tile < shore_base + 0x2A;
}

// 0x486380：这一格还没定砖
inline bool IsUndecidedTile(int tile) { return tile == 0 || tile == 0xFFFF; }

// 0x4863D0（ecx = cell）与 0x578D80（ecx = tile, edx = subtile）：
// 「这块地形不许被覆盖」。两者判据几乎一样，差别是前者读格子、后者读砖号+子格号，
// 且前者对每个全局都先查 -1（该 theater 没定义就跳过），后者不查。照原样各写一份。
bool IsProtectedTerrainCell(const TheaterData& t, int tile, int subtile);
bool IsProtectedTerrainTile(const TheaterData& t, int tile, int subtile);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_PLACE_TILE_H_
