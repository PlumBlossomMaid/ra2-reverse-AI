#include "start_points.h"

#include <algorithm>

#include "fast_math.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {

// 0x89F688，8 项 {int16 dx, int16 dy}，运行期由 0x49F2F0 写入
const int kAdjacentCellDX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int kAdjacentCellDY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

namespace {

inline PaintWorkCell& Work(StartPointContext& ctx, const CellCoord& c) {
  return (*ctx.work)[static_cast<std::size_t>(c.y * ctx.side + c.x)];
}
inline const PaintWorkCell& Work(const StartPointContext& ctx,
                                 const CellCoord& c) {
  return (*ctx.work)[static_cast<std::size_t>(c.y * ctx.side + c.x)];
}

// 0x594F40 / 0x594870 里那句 `if (工作数组指针 == 0) owner = -1`。
// 原版真的每次循环都重新加载 [0xABED10] 并判一次空，照抄。
inline int OwnerOf(const StartPointContext& ctx, const CellCoord& c) {
  if (ctx.work == nullptr) return -1;
  return Work(ctx, c).blob_id;
}

// 「两点距离 + 跨区奖励」。原版是 `sub / neg / sbb / and 0x14` 的无分支写法，
// 两个点不属于同一个区域时白送 20 格。
inline double PairScore(int dx, int dy, bool cross_region) {
  const double d = FastSqrt(static_cast<double>(dy * dy + dx * dx));
  return cross_region ? d + 20.0 : d;
}

}  // namespace

int StartRegionSortKey(int cell_count, int region_id, int region_count) {
  // 0x594D0x 附近：(500000 - cellCount) * regionCount + regionId。
  // ⚠️ regionCount 取的是**销毁小区域之前**的个数（iVar11 在销毁循环之前
  // 就已经定住），不是排序时的存活个数。
  return (500000 - cell_count) * region_count + region_id;
}

std::vector<int> AllocateStartQuota(const std::vector<int>& cell_counts,
                                    int total_players) {
  const int n = static_cast<int>(cell_counts.size());
  std::vector<int> out(static_cast<std::size_t>(n), 0);
  if (n <= 0) return out;
  // 0x594DD9：先把全部格子数加起来当分母
  int all_cells = 0;
  for (int i = 0; i < n; ++i) all_cells += cell_counts[i];

  int acc = 0, assigned = 0;
  for (int i = 0; i < n; ++i) {
    acc += cell_counts[i];
    int give;
    if (i == n - 1) {
      give = total_players - assigned;          // 0x594E1D，最后一个吃余数
    } else {
      // 0x594E25：fild acc / fidiv allCells / fimul players / fadd 0.5
      //           / fisub assigned / _ftol（截断）
      // ⚠️ `+0.5` 排在 `-assigned` **之前**，浮点上与另一种写法不等价。
      double v = static_cast<double>(acc);
      v /= static_cast<double>(all_cells);
      v *= static_cast<double>(total_players);
      v += 0.5;
      v -= static_cast<double>(assigned);
      give = Ftol(v);
    }
    out[static_cast<std::size_t>(i)] = give;
    assigned += give;
  }
  return out;
}

RegionClass* GrowStartRegion(StartPointContext& ctx, const CellCoord& seed,
                             int movement_zone, int want_type) {
  // 0x594420。⚠️ 原版的 RegionClass 构造 0x58BF70 会顺带从种子格读 level 与
  // wet，但阶段 6 之后没有任何人读这两个字段，所以这里不建模。
  RegionClass* r = ctx.regions->Create(seed, 0, false);
  const int id = r->id;

  std::vector<CellCoord> stack;      // 栈上的 DynamicVectorClass，步长 20000
  Work(ctx, seed).queued = id;       // +0x3C，**先标记再入栈**
  stack.push_back(seed);

  while (!stack.empty()) {
    const CellCoord cur = stack.back();
    stack.pop_back();
    Work(ctx, cur).blob_id = id;     // +0x38
    ++r->cell_count;                 // +0x0C
    r->cells.push_back(cur);         // +0x28

    for (int d = 0; d < 8; ++d) {
      const CellCoord n = {cur.x + kAdjacentCellDX[d], cur.y + kAdjacentCellDY[d]};
      // ⚠️ 先取格再判界：越界时 0x5657A0 返回共享哑元，并把请求坐标写进它的
      // +0x24。随后读回来的坐标就是刚写进去的那份，所以界外邻格照样参与
      // 后面的菱形判定（只是必然判假）。这个写副作用能被别处读到，必须复现。
      const CellCoord rb = ctx.world->ReadBackCoord(n);
      if (!ctx.grid.InMap(rb)) continue;
      if (ctx.world->CellZoneId(rb) != 0) continue;
      if (ctx.world->MovementZoneType(rb, movement_zone) != want_type) continue;
      PaintWorkCell& w = Work(ctx, rb);
      if (w.queued != -1) continue;
      w.queued = id;
      stack.push_back(rb);
    }
  }
  return r;
}

std::vector<CellCoord> ChooseWaypoints(const StartPointContext& ctx,
                                       const std::vector<CellCoord>& cand,
                                       int start_count, bool* ok) {
  std::vector<CellCoord> out;
  if (ok != nullptr) *ok = true;
  const int n = static_cast<int>(cand.size());

  // 0x594F49：want = (int)((scale * 0.01 * 12.0 / NumPlayers + 2.0) * startCount)
  double v = static_cast<double>(ctx.waypoint_scale);
  v *= 0.01;
  v *= 12.0;
  v /= static_cast<double>(ctx.num_players);
  v += 2.0;
  v *= static_cast<double>(start_count);
  int want = Ftol(v);

  if (start_count == 0 && n > 0) want = n;          // 0x594F9D 之后
  if (n < want || want == 0) {
    want = n;
    if (start_count == 0) {                          // 原版这里 return NULL
      if (ok != nullptr) *ok = false;
      return out;
    }
  }

  if (n >= 2) {
    // 第一个点：两两配对里得分最高的那一对，取**外层下标** i
    double best = -1.0;
    int best_i = -1;
    for (int i = 0; i < n - 1; ++i) {
      const int ra = OwnerOf(ctx, cand[static_cast<std::size_t>(i)]);
      for (int j = i + 1; j < n; ++j) {
        const int rb = OwnerOf(ctx, cand[static_cast<std::size_t>(j)]);
        const int dx = cand[static_cast<std::size_t>(i)].x -
                       cand[static_cast<std::size_t>(j)].x;
        const int dy = cand[static_cast<std::size_t>(i)].y -
                       cand[static_cast<std::size_t>(j)].y;
        const double s = PairScore(dx, dy, rb != ra);
        if (s > best) { best = s; best_i = i; }      // 严格大于
      }
    }
    if (best_i == -1) return out;                    // 够不着，n >= 2 时不会发生
    out.push_back(cand[static_cast<std::size_t>(best_i)]);
  } else {
    if (n != 0) {
      // ⚠️ 只有一个候选时**直接返回**，不管 want 是几
      out.push_back(cand[0]);
      return out;
    }
    // n == 0：want 必然已经被压成 0，下面的补点循环一次都不进
  }

  // 补点：每轮挑「到已选点集的最小距离」最大的那个候选
  while (static_cast<int>(out.size()) < want) {
    double best = -1.0;
    int best_i = -1;
    for (int i = 0; i < n; ++i) {
      const int ra = OwnerOf(ctx, cand[static_cast<std::size_t>(i)]);
      double mn = 9999999.0;      // 0x416312CFE0000000，**不是** 1e7
      for (std::size_t k = 0; k < out.size(); ++k) {
        const int rb = OwnerOf(ctx, out[k]);
        const int dx = cand[static_cast<std::size_t>(i)].x - out[k].x;
        const int dy = cand[static_cast<std::size_t>(i)].y - out[k].y;
        const double d = PairScore(dx, dy, rb != ra);
        if (d < mn) mn = d;       // 严格小于
      }
      if (best < mn) { best = mn; best_i = i; }      // 严格大于
    }
    // ⚠️ 已选过的候选不会从表里删掉，它的 mn 会变成 0；而 best 初值是 -1，
    // 于是名额没用完时**同一个格子可以被选进来第二次**。照抄，不要去重。
    if (best_i == -1) break;      // 原版这里会读 cand[-1]，n == 0 时才可能
    out.push_back(cand[static_cast<std::size_t>(best_i)]);
  }
  return out;
}

// 0x594B60 起摆在栈上的 {5, 5, 5, 0, 0}
const int kStartZoneSel[5] = {5, 5, 5, 0, 0};

void ComputeStartZoneSelection(StartPointContext& ctx, ZoneTable& zones) {
  const ZoneResult r = zones.Rebuild();                 // 0x56C510
  const int mz = kStartZoneSel[ctx.map_type & 7];
  ctx.movement_zone = mz;
  ctx.want_zone_type = static_cast<int>(
      r.zone_type[static_cast<std::size_t>(mz)]
                 [static_cast<std::size_t>(r.largest_zone)]);
}

void PickPointsInRegion(StartPointContext& ctx, RegionClass* r, int base) {
  // 0x594876：可用矩形再往里缩一圈
  MapRect rect;
  rect.x = ctx.usable.x;
  rect.y = ctx.usable.y;
  rect.w = ctx.usable.w;
  rect.h = ctx.usable.h;

  std::vector<CellCoord> cand;

  // 取数点 1（0x5948CD）：PickBelow(3)。⚠️ startCount == 0 的区域也照抽不误。
  const int extra = PickBelow(*ctx.rng, 3);
  const int budget = extra + r->start_points * 15;

  for (int tries = 0;
       static_cast<int>(cand.size()) < budget && tries < 300; ++tries) {
    // 取数点 2（0x594935）：**两次乘法**那一路（先 (double)n 再 x 1/(2^32-1)），
    // 与 PickBelow 的「预折叠成一次乘法」在末位会分岔，不能互换。
    const int n = static_cast<int>(r->cells.size());
    const int k = PickBelowMul(*ctx.rng, n);
    const CellCoord xy = r->cells[static_cast<std::size_t>(k)];
    if (!ctx.world->AreaIsPassable(xy)) continue;        // 0x5A7250，{X-3,Y-3,6,6}
    if (!ctx.world->WithinUsableArea(rect, xy)) continue; // 0x578640(rect, xy, 1)
    cand.push_back(xy);
  }

  bool ok = false;
  std::vector<CellCoord> out = ChooseWaypoints(ctx, cand, r->start_points, &ok);
  if (!ok) return;                                       // 原版 return NULL 那条

  ctx.world->ResetStartLocationTable();                  // 0x5950C2
  for (int i = 0; i < r->start_points; ++i) {
    // ⚠️ out.size() 可能小于 startCount（候选不够时 want 被压成候选数）。
    // 原版此时读的是向量容量内的**未初始化内存**，那是真·未定义，复不出来；
    // 这里直接停手，并在文档里标成已知分歧点。
    if (i >= static_cast<int>(out.size())) break;
    ctx.world->SetWaypoint(base + i, out[static_cast<std::size_t>(i)]);
    ctx.world->SetStartLocation(i, out[static_cast<std::size_t>(i)]);
  }
  // 把已经用掉的那几个从表头逐个挪走（原版是 startCount 次「删首元素」）
  for (int i = 0; i < r->start_points; ++i) {
    if (out.empty()) break;
    out.erase(out.begin());
  }
  r->candidates = out;                                   // 覆盖 RegionClass +0x00
}

bool PlaceStartPoints(StartPointContext& ctx) {
  // 第 0 步的后半段：拆掉上一轮的全部区域（0x58CE90）
  ctx.regions->Reset();

  // 第 1 步：minCells = (int)max(H * W * 0.03, 400.0)
  double area = static_cast<double>(ctx.map_h) * static_cast<double>(ctx.map_w);
  area *= 0.03;
  if (area < 400.0) area = 400.0;
  const int min_cells = Ftol(area);

  // 第 2 步：扫全图起种
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    if (Work(ctx, c).blob_id != -1) return;              // work +0x38
    if (ctx.world->CellZoneId(c) != 0) return;           // cell +0x4C
    if (ctx.world->MovementZoneType(c, ctx.movement_zone) != ctx.want_zone_type)
      return;
    GrowStartRegion(ctx, c, ctx.movement_zone, ctx.want_zone_type);
  });

  // ⚠️ 排序键里的「区域个数」定在销毁之前
  const int region_count_before = ctx.regions->Count();

  // 倒序销毁格数不足的区域
  for (int i = ctx.regions->Count() - 1; i >= 0; --i) {
    RegionClass* r = ctx.regions->At(i);
    if (r->cell_count < min_cells) ctx.regions->Destroy(r);
  }

  // 第 3 步：按键升序稳定排序（原版是 qsort 0x7C8B48 + 比较器 0x5AD9C0；
  // 键里已经把 regionId 当低位，不存在相等的两项，所以稳定性无所谓）
  std::vector<RegionClass*> list;
  for (int i = 0; i < ctx.regions->Count(); ++i) list.push_back(ctx.regions->At(i));
  std::vector<std::pair<int, RegionClass*> > keyed;
  for (std::size_t i = 0; i < list.size(); ++i) {
    keyed.push_back(std::make_pair(
        StartRegionSortKey(list[i]->cell_count, list[i]->id, region_count_before),
        list[i]));
  }
  std::sort(keyed.begin(), keyed.end(),
            [](const std::pair<int, RegionClass*>& a,
               const std::pair<int, RegionClass*>& b) {
              return a.first < b.first;
            });

  // 第 4 步：按面积比例分名额
  std::vector<int> counts;
  for (std::size_t i = 0; i < keyed.size(); ++i) {
    counts.push_back(keyed[i].second->cell_count);
  }
  const std::vector<int> quota = AllocateStartQuota(counts, ctx.num_players);
  for (std::size_t i = 0; i < keyed.size(); ++i) {
    keyed[i].second->start_points = quota[i];
  }

  // 第 5 步：逐区域摆点，base 是前面各区域名额的累计
  int base = 0;
  for (std::size_t i = 0; i < keyed.size(); ++i) {
    PickPointsInRegion(ctx, keyed[i].second, base);
    base += keyed[i].second->start_points;
  }

  // 第 6 步：MapType == 0 时给空手的区域各抛一次硬币
  if (ctx.map_type == 0) {
    for (std::size_t i = 0; i < keyed.size(); ++i) {
      if (keyed[i].second->start_points != 0) continue;
      if (UniformUnit(*ctx.rng) < 0.5) {                 // 0x594EC1，严格小于
        ctx.world->PlaceRandomBuilding(keyed[i].second);
      }
    }
  }
  return true;                                           // 原版所有出口都 return 1
}

}  // namespace rmg
}  // namespace ra2
