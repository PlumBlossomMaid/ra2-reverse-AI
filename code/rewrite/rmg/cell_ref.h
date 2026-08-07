// 「格子指针」的忠实模型：原版拿不到真实格子时会退到一份**全局共享的哑元**。
//
// `MapClass::GetCellAt`（0x5657A0）按 `(int16 Y << 9) + int16 X` 查指针表；
// 下标越界或表里是空指针，就返回静态哑元 0xABDC50，并把请求的坐标写进它的
// +0x24。哑元只有一份，上一次留下的 +0x38 会被下一次读到——这不是实现细节，
// 是能改变地图产物的行为（岸线走线、阶段 2 的刷绿都会碰到），必须复现。
//
// 取证（gamemd.exe）：0x481810 AdjacentCell(dir) → 0x5657A0 GetCellAt(coord)，
// 方向表 0x89F688 由 0x49F2F0 在运行期填出。
#ifndef RA2_RMG_CELL_REF_H_
#define RA2_RMG_CELL_REF_H_

#include "map_grid.h"
#include "shore_smooth.h"

namespace ra2 {
namespace rmg {

struct CellRef {
  bool dummy = false;
  CellCoord coord;
};

// 八方向坐标增量（0x89F688）：0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW。
extern const CellCoord kAdjacentDelta[8];

CellRef RefOf(const CellCoord& c);
CellRef Lookup(SmoothWorld& w, int x, int y);          // 0x5657A0
CellRef Adjacent(SmoothWorld& w, const CellRef& r, int dir);   // 0x481810

CellCoord CoordOf(const SmoothWorld& w, const CellRef& r);
int TileOf(const SmoothWorld& w, const CellRef& r);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_CELL_REF_H_
