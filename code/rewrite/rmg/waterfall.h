// 瀑布 / 河道落差 `FUN_0059E740`——河流生成器里唯一会把地形抬高的零件。
//
// 河每走一步都可能触发它一次（`0x59DD1C`）。它先在河的行进方向前方扫一片
// 净空区，确认没人占着；然后涂上游一片水、把整块台地抬高 4 层、再涂下游
// 一片水（这一片留在低处），最后在两片水之间那道陡坎上贴四块瀑布砖。
// 成功之后调用方的游标被**直接推进 12 格**，河道在这里断开重画。
//
// ⚠️ 它**不是桥**。文档初稿把它记成了架桥，成因见
// docs/random-map/stage-water-maptype34.md §八末尾。判据是取砖点只引用四个
// 瀑布 TileSet 全局：dir 0 → `[0xAA10A0]` WaterfallNorth、
// 2 → `[0xAA073C]` East、4 → `[0xAA1050]` South、6 → `[0xABB110]` West。
//
// 取证（gamemd.exe）：主体 0x59E740-0x5A004D，两张跳转表 0x5A0050（按 dir
// 拼矩形）与 0x5A006C（按 dir 贴砖）。
//   净空扫描   0x59E9FA-0x59EACD（唯一的"此处不能放"出口，不取随机数）
//   上游水面   0x59EAD2-0x59EC49（每格 2 次拒绝采样）
//   台地三连   0x59EC49 / 0x59EC86 / 0x59ECCE（AngularGrow → Shoreline → Dilate）
//   整体抬升   0x59ECDF-0x59ED4B（走地图迭代器，+0x11B += 4）
//   下游水面   0x59ED4B-0x59EECA（又是每格 2 次）
//   贴砖       0x59EEE8(dir2) / 0x59F2C4(dir6) / 0x59F77C(dir4) / 0x59FB3F(dir0)
//   瞬移 12 格 0x5A0008-0x5A0035
// 详见 docs/random-map/stage-water-maptype34.md §六。
#ifndef RA2_RMG_WATERFALL_H_
#define RA2_RMG_WATERFALL_H_

#include "place_tile.h"
#include "regions.h"

namespace ra2 {
namespace rmg {

// 瀑布同时要扮演区域算子的世界（RegionWorld）与贴砖器的世界（TileWorld）。
//
// ⚠️ `CellExists` / `SetDummyCoord` / `IsoTile` / `SetIsoTile` / `SetSubTile`
// / `SetLevel` 在两个基类里各声明一次。实现方**写一份**即可同时覆盖，但调用点
// 的非限定名查找会二义，所以这里给两个非虚壳子统一入口。
class WaterfallWorld : public RegionWorld, public TileWorld {
 public:
  int Tile(const CellCoord& c) const {
    return static_cast<const RegionWorld*>(this)->IsoTile(c);
  }
  void SetTile(const CellCoord& c, int v) {
    static_cast<RegionWorld*>(this)->SetIsoTile(c, v);
  }

  // 0x57A0C0(blob_id, 0)。⚠️ flag 是 0。
  virtual bool SmoothShoreline(int blob_id) = 0;

  // `[0xA8ED2C]` 全局砖表；越界或该砖未装载时返回 nullptr。
  virtual const TileType* TileByIndex(int index) const = 0;
};

// 两个块编号必须分开存：涂水时写进工作数组的是**入参** `blob_id`
// （0x59EC1E / 0x59EE9E 取 `[esp+0x12C]`），而 AngularGrow / Shoreline /
// Dilate / 抬升判定 / 贴砖 / 那几格 0xFFFF 用的都是**当场读**的
// `MapSeedClass +0x308`（0x59EC58 起）。调用点上二者相同，照读不折叠。
struct WaterfallParams {
  int blob_id = 0;     // 入参 arg1
  int blob_stamp = 0;  // MapSeedClass +0x308
};

// `MapSeedClass::PlaceWaterfall(blobId, a, b, dir, okOut, x, y)`
// （0x59E740，`ret 0x1C`）。
//   a / b   河当前横截面的两端（真实坐标）
//   dir     河的行进方向，取值 0 / 2 / 4 / 6；其余值让整个函数空转
//   okOut   出参。⚠️ 进场先写 0，之后每一条早退路径都**不再动它**——
//           于是调用方看到"此处不适合放瀑布，跳过"，而返回值 1 只是内部约定。
//   x / y   调用方的游标。成功时被推进 12 格（dx/dy 见 0x5A0008 那两张小表）。
//
// ⚠️ 必须在 FpuChopScope 作用域内调用（两片水的拒绝采样走 x87）。
bool PlaceWaterfall(RegionContext& ctx, WaterfallWorld& world,
                    const WaterfallParams& p, const CellCoord& a,
                    const CellCoord& b, int dir, bool* ok_out, double* x,
                    double* y);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_WATERFALL_H_
