// 涂块核心 `FUN_0059BBC0`：从一个种子格出发，用最小堆把水抠成陆地。
//
// 这是整张地图"陆地长什么样"的唯一来源。算法是 Dijkstra 式扩散：
// 每次取出优先级最小的候选格变成陆地，再把它的四个等距邻格塞回堆；
// 优先级 = FastSqrt(到引力中心的距离) + 均匀噪声，而**引力中心自己在做布朗运动**。
//
// 取证（gamemd.exe）：主体 0x59BBC0-0x59C52C，随机起点 0x59BC89-0x59BDDF，
// 邻域扫描 0x59C047-0x59C2B6，中心游走 0x59C2BC，收尾/回滚 0x59C332-0x59C512。
// 详见 docs/random-map/stage-seeding-water.md §四。
#ifndef RA2_RMG_BLOB_PAINT_H_
#define RA2_RMG_BLOB_PAINT_H_

#include <vector>

#include "blob_region.h"
#include "fast_math.h"
#include "map_grid.h"
#include "randomizer.h"
#include "theater.h"

namespace ra2 {
namespace rmg {

// 工作数组的一条记录里，涂块只碰这三个字段（记录本身 0x50 字节）。
// 完整布局见 stage-init.md §五 / stage-seeding-water.md §七。
struct PaintWorkCell {
  int blob_id = 0;   // +0x38 归属块编号，0 = 尚未属于任何块
  int queued = 0;    // +0x3C 已入堆标记，值是当前块编号（靠"编号自增"批量作废）
  bool flag4b = false;  // +0x4B 回滚时被清零；含义未确认

  // +0x00 打包坐标 (Y<<16)|X。阶段 1 只对**菱形内**的格子写它（stage-init.md §五），
  // 菱形外的槽位永远是 (0,0)——阶段 3 的三处线性扫描就是靠 `coord != (0,0)`
  // 把它们滤掉的（0x58CFB8 / 0x5A073C / 0x58D02A）。涂块本身不读这一项。
  CellCoord coord;

  // 下面三项属于阶段 9「造山」，与上面几项住在**同一条 0x50 字节记录**里。
  double height = 0.0;   // +0x08 连续高度场（0x5A2F50 写，四舍五入远离零后落地）
  double slope = 0.0;    // +0x10 起伏强度，充当下一次正态抽样的 sigma
  // +0x45 一个字节被两拨代码共用：阶段 6 的 0x5A1FB0 拿它记「被出生点洪泛
  // 辐射过」，阶段 9 的 0x5A33F0 拿它当「锚点，不许抬升」。语义未定名，
  // 所以这里按偏移命名。
  bool flag45 = false;

  // 阶段 10「Creating LATs」用的三个概率槽，也住在同一条 0x50 字节记录里。
  // 播种点 0x5A38C0（温带按 Vegetation 算）与 0x5992xx（雪地写死两个常量）。
  double prob_rough = 0.0;   // +0x18 碎石 LAT（RoughTile）
  double prob_green = 0.0;   // +0x20 草地 LAT（GreenTile）
  double prob_sand = 0.0;    // +0x28 沙地 LAT（SandTile）

  // +0x47 树林补丁的「走过了」标记。⚠️ 0x5A45E0 返回后**不清**，
  // 所以 100 轮补丁互相排斥（stage-lats.md §七 未确认 7）。
  bool flag47 = false;
};

// 涂块需要的全部外部依赖。抽出来是为了让算法本身可以脱离引擎单测——
// 每个方法都对应原版的一次具体调用，注释里给了地址。
class PaintWorld {
 public:
  virtual ~PaintWorld() = default;

  // 0x4865D0 IsPaintable：这一格允许被涂成陆地吗
  virtual bool IsPaintable(const CellCoord& c) const = 0;

  // CellClass +0x38 = IsoTileTypeIndex
  virtual int IsoTile(const CellCoord& c) const = 0;
  virtual void SetIsoTile(const CellCoord& c, int tile) = 0;

  // CellClass +0x11A / +0x11B（回滚与去岸线时被写）
  virtual void SetCellSubTile(const CellCoord& c, int v) = 0;   // +0x11A
  virtual void SetCellLevel(const CellCoord& c, int v) = 0;     // +0x11B

  // 0x57A0C0(blob_id, 1)：给这一块铺水陆过渡砖；铺不出来则整块作废
  virtual bool SmoothShoreline(int blob_id) = 0;

};

// 八个入口参数，逐个对应 0x59BBC0 的栈帧（ret 0x20 = 8 个 dword）。
struct PaintParams {
  int budget = 0;             // [ebp+0x08] 预算；<= 400 一律提到 400
  Rect region;                // [ebp+0x0C] 投放区域（真实坐标）
  CellCoord start;            // [ebp+0x10] 起点；两分量都为 0 时随机取
  bool ellipse = false;       // [ebp+0x14] 0 = 矩形，非 0 = 内切椭圆
  CellCoord target;           // [ebp+0x18] 方向性代价的目标点
  double walk_sigma = 0.0;    // [ebp+0x1C] 引力中心每步的游走标准差
  bool directional = false;   // [ebp+0x24] 优先级模式：0 = 距离+抖动
};

// 算法跑起来需要的全局环境。
struct PaintContext {
  MapGrid grid{0, 0};
  Rect full_bounds;              // [0xABE2F0..FC]
  std::vector<PaintWorkCell>* work = nullptr;   // [0xABED10]，side² 条
  PaintWorld* world = nullptr;
  const TheaterData* theater = nullptr;   // [0xAA0738] / [0xABAD28] 的来源
  Randomizer* rng = nullptr;
  NormalDeviate* normal = nullptr;
  int* blob_stamp = nullptr;     // MapSeedClass +0x308：块编号计数器
  int cell_level = 4;            // MapSeedClass +0x30C，回滚时写回 +0x11B
};

// 返回涂成功的格数；起点找不到、或岸线铺不出来导致整块回滚时返回 0。
// ⚠️ 必须在 FpuChopScope 作用域内调用。
int PaintBlob(PaintContext& ctx, const PaintParams& params);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_BLOB_PAINT_H_
