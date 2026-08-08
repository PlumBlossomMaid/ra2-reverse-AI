// 阶段 10「Creating LATs, rocks etc」——地表装饰。
//
// 整条流水线里取数点最密的一段（18 个）。按 theater 分两条互斥的路：
//   温带（Theater == 0）：0x5A38C0 按 Vegetation 播三张概率图 → 0x5A3AE0 主体
//   雪地（Theater == 1）：调度处内联铺两个常量           → 0x5A4280 简化主体
// 两条主体都靠同一个 0x5A4B60「LAT 块写入器」落地，也都调同一个
// 0x5A45E0「树林补丁生长」。
//
// 取证（gamemd.exe）：
//   0x59921B  调度（Theater == 0 分支）
//   0x5A38C0  播概率图（0 取数；含「5×5 邻域写的全是中心格」那个原版 bug）
//   0x5A3AE0  温带主体（取数点 5A3B58 / 5A3B88 / 5A3BB8 / 5A3C53 / 5A3D3F /
//             5A3E1B / 5A40F0 / 5A4157 / 5A41DE / 5A4221）
//   0x5A4280  雪地主体（取数点 5A434D / 5A447D）
//   0x5A45E0  树林补丁（取数点 5A47F2 / 5A481C / 5A49B8）
//   0x5A4B60  LAT 块写入器（取数点 5A4E6E）
//   0x42B1F0  n = (MapRect.Height + 4) * MapRect.Width * 2
//   0x486380 未定砖 / 0x4865B0 岸砖 / 0x486790 沙 LAT / 0x4867B0 绿地
// 详见 docs/random-map/stage-lats.md。
#ifndef RA2_RMG_LATS_H_
#define RA2_RMG_LATS_H_

#include <vector>

#include "blob_paint.h"   // PaintWorkCell：+0x18/+0x20/+0x28/+0x3C/+0x45/+0x47
#include "fast_math.h"    // FastSqrt / NormalDeviate
#include "map_grid.h"
#include "randomizer.h"
#include "theater.h"

namespace ra2 {
namespace rmg {

// 本阶段要碰的格子字段。访问器全部按 0x5657A0 GetCellAt 的口径：拿不到实体格
// 就退到那份**全局共享的哑元** 0xABDC50（只有一份，残留值会被下一次读到）。
class LatWorld {
 public:
  virtual ~LatWorld() {}

  virtual int IsoTile(const CellCoord& c) = 0;             // +0x38
  virtual void SetIsoTile(const CellCoord& c, int v) = 0;
  virtual int Ramp(const CellCoord& c) = 0;                // +0x11C
  virtual int Overlay(const CellCoord& c) = 0;             // +0x44，-1 = 没有
  virtual void SetOverlay(const CellCoord& c, int v) = 0;
  virtual void SetOverlayData(const CellCoord& c, int v) = 0;   // +0x11E
  virtual int Field0xE4(const CellCoord& c) = 0;
  virtual int Field0xEC(const CellCoord& c) = 0;

  // 0x486670 = 砖号 ∈ [PaveTile, +0x10)；0x486650 = ∈ [MiscPaveTile, +0x0E)。
  // 原版是两个独立的谓词，短路求值前者在先，这里照样分开两个。
  // 只有 0x5A4B60 的 allowNonPlain 分支读它们，而阶段 10 三个调用点全传 0 ——
  // 死分支，形状保留（见 stage-making-regions.md §五之三）。
  virtual bool IsPaveTile(const CellCoord& c) = 0;
  virtual bool IsMiscPaveTile(const CellCoord& c) = 0;

  // 0x4865B0：这一格是岸砖（砖号 ∈ [ShorePieces, +0x2A)）
  virtual bool IsShorePiece(const CellCoord& c) = 0;
  // 0x486790：这一格是沙地 LAT（砖号 ∈ [ClearToSandLat, +0x10)）
  virtual bool IsClearToSandLat(const CellCoord& c) = 0;
  // 0x4867B0：这一格是绿地（GreenTile 本身或 ClearToGreenLat 的 16 块）
  virtual bool IsGreen(const CellCoord& c) = 0;

  // 0x47CA80 CellClass::SetupLAT：把涂出来的 LAT 块换成正确的过渡片。无随机。
  virtual void SetupLAT(const CellCoord& c) = 0;
  // 0x47D2B0 CellClass::RecalcAttributes(-1)
  virtual void RecalcAttributes(const CellCoord& c, int arg) = 0;
  // 0x5A48xx：new(0xE0) + TerrainClass::TerrainClass(TerrainTypes[FindIndex(name)],
  // &cell->+0x24)。name 恒为 "TREE01".."TREE25"（"TREE00" 永不出现）。
  virtual void CreateTerrain(const char* name, const CellCoord& at) = 0;
};

// MapSeedClass / rules.ini / MapClass 里这一阶段读到的全部标量。
// 按 CLAUDE.md 的数据驱动要求，`max_trees` 必须从 rules.ini [General] MaxTrees 注入。
struct LatParams {
  int theater = 0;        // MapSeedClass +0x38，0 = 温带，1 = 雪地（只有这两个）
  int vegetation = 50;    // +0x5C
  int width = 64;         // +0x64，树林预算里的 Width
  int max_trees = 500;    // +0x2FC = rules.ini [General] MaxTrees
  int map_rect_w = 64;    // [0x87F8DC]，0x42B1F0 用
  int map_rect_h = 64;    // [0x87F8E0]
};

struct LatContext {
  MapGrid grid{0, 0};
  std::vector<PaintWorkCell>* work = nullptr;   // [0xABED10]，side² 条
  LatWorld* world = nullptr;
  Randomizer* rng = nullptr;                    // [0xABE890]
  NormalDeviate* normal = nullptr;              // [0xABDFB8]，缓存跨阶段串
  int side = 0;                                 // [0x89C2DC] = W + H + 1
  const TheaterData* theater = nullptr;         // 那几个 TileSet 全局的来源
  LatParams params;
};

// 0x5A4B60 FUN(seed, tileIndex, count, fieldId, allowNonPlain)。
// 「铺满 count 格」的最小堆扩散：每弹出一格就把它的 +0x38 写成 tileIndex。
void WriteLatBlock(LatContext& ctx, const CellCoord& seed, int tile_index,
                   int count, int field_id, bool allow_non_plain);

// 0x5A45E0 FUN(seed, size, prob)。返回**真正栽下的树数**（调用方从预算里扣）。
int GrowTreePatch(LatContext& ctx, const CellCoord& seed, int size, double prob);

// 0x5A38C0（__fastcall ecx = MapSeedClass）：按 Vegetation 播三张概率图。0 取数。
void SeedVegetationProbabilities(LatContext& ctx);

// 0x5A3AE0：温带主体。
void CreateLatsTemperate(LatContext& ctx);

// 0x5A4280：雪地主体。
void CreateLatsSnow(LatContext& ctx);

// 0x59921B 那段调度：温带走 0x5A38C0 + 0x5A3AE0，其余（= 雪地）内联铺常量
// 再走 0x5A4280。
void CreateLats(LatContext& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_LATS_H_
