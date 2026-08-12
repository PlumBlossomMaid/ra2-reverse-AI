#include "corner_lattice.h"

#include <cstdint>

#include "height_field.h"   // MarkHeightAnchors / GenerateHeightField
#include "scaled_random.h"  // Ftol

namespace ra2 {
namespace rmg {
namespace {

// 0x5657A0 的结果：要么是实体格，要么是那份全局共享的哑元。
struct HillRef {
  bool dummy = false;
  CellCoord c;
};

// 0x5657A0 GetCellAt：下标 = (int16 Y << 9) + int16 X，越界或槽位为空就退到
// 哑元并把请求坐标写进 0xABDC74。
HillRef LookupCell(HillWorld& w, int x, int y) {
  const CellCoord c{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)};
  const int index = (c.y << 9) + c.x;
  if (index < 0 || index >= 0x40000 || !w.CellExists(c)) {
    w.SetDummyCoord(c);
    return HillRef{true, c};
  }
  return HillRef{false, c};
}

int RampOf(const HillWorld& w, const HillRef& r) {
  return r.dummy ? w.DummyRamp() : w.Ramp(r.c);
}

// 4 个角在格网里的行优先偏移与「北东南西」环形角序的对应：
//   0 北 = (cx,   cy)      1 东 = (cx+1, cy)
//   2 南 = (cx+1, cy+1)    3 西 = (cx,   cy+1)
void CornerHeights(const CornerLattice& lat, int cx, int cy, int* h) {
  h[0] = lat.At(cx, cy).height;
  h[1] = lat.At(cx + 1, cy).height;
  h[2] = lat.At(cx + 1, cy + 1).height;
  h[3] = lat.At(cx, cy + 1).height;
}

}  // namespace

// 0x83FF18。取证：19 个 int[4] 指针，循环上界硬编码 0x83FF64。
const int kRampTable[19][4] = {
    {0, 0, 0, 0},      //  0 平地（表项指向 .bss 0xB0B6DC）
    {0, 15, 15, 0},    //  1
    {0, 0, 15, 15},    //  2
    {15, 0, 0, 15},    //  3
    {15, 15, 0, 0},    //  4
    {0, 0, 15, 0},     //  5
    {0, 0, 0, 15},     //  6
    {15, 0, 0, 0},     //  7
    {0, 15, 0, 0},     //  8
    {0, 15, 15, 15},   //  9
    {15, 0, 15, 15},   // 10
    {15, 15, 0, 15},   // 11
    {15, 15, 15, 0},   // 12
    {0, 15, 30, 15},   // 13
    {15, 0, 15, 30},   // 14
    {30, 15, 0, 15},   // 15
    {15, 30, 15, 0},   // 16
    {0, 15, 0, 15},    // 17
    {15, 0, 15, 0},    // 18
};

bool IsRaisable(const HillContext& ctx, const CellCoord& c) {
  if (!ctx.grid.InMap(c)) return false;                 // 0x5AC230
  if (ctx.world->Overlay(c) != -1) return false;        // +0x44
  if (ctx.world->HasObject(c)) return false;            // +0xE4
  // 0x58C2A0：工作记录 = work[(Y * side + X)]
  const PaintWorkCell& w =
      (*ctx.work)[static_cast<std::size_t>(ctx.grid.WorkIndex(c))];
  if (w.flag45) return false;                           // +0x45 锚点
  return ctx.world->TileMorphable(ctx.world->IsoTile(c));
}

void BuildCornerLattice(HillContext& ctx) {
  CornerLattice& lat = ctx.lattice;
  HillWorld& world = *ctx.world;

  // MapClass::Set_Dimensions（0x56631C-0x566338）把 +0x124..+0x130 写成
  // {1, 1, W+H-1, W+H-1}，其中 W/H 是**传进去的**那个矩形（阶段 1 传
  // {0,0,mapW+4,mapH+12}），也就是这里 grid 的 width()/height()。
  // 0x6B2A70 再各 +1 得到格网的列数与行数。
  lat.origin_x = 1;
  lat.origin_y = 1;
  lat.stride = ctx.grid.width() + ctx.grid.height();
  lat.rows = lat.stride;
  lat.cells.assign(static_cast<std::size_t>(lat.stride) * lat.rows,
                   CornerEntry());

  // 找代表格用的三个增量：自身 -> 北 -> 西北 -> 西（每次都是**从自身**偏移，
  // 不是累加）。0x6B2AF8 起那三个立即数 0xFFFF0000 / 0xFFFFFFFF / 0x0000FFFF。
  static const CellCoord kProbe[3] = {{0, -1}, {-1, -1}, {-1, 0}};

  for (int y = 0; y < lat.rows; ++y) {
    for (int x = 0; x < lat.stride; ++x) {
      CornerEntry& e = lat.At(x, y);
      e.locked = false;
      e.dirty = false;
      int height = 0;

      const CellCoord base{static_cast<std::int16_t>(x + lat.origin_x),
                           static_cast<std::int16_t>(y + lat.origin_y)};
      CellCoord cand = base;
      for (int k = 0; !ctx.grid.InMap(cand) && k <= 2; ++k) {
        cand = CellCoord{static_cast<std::int16_t>(base.x + kProbe[k].x),
                         static_cast<std::int16_t>(base.y + kProbe[k].y)};
      }

      if (ctx.grid.InMap(cand)) {
        // 代表格的**北角**高度：坡面表第 0 项 + 层高 x 15
        const int ramp = world.Ramp(cand);
        height = kRampTable[ramp][0] + world.Level(cand) * 15;

        // 这个角点被四个格子共用；任何一个共用格「不可抬升」，角点就锁死。
        // 只有落在菱形内的那几个格子参与判定，菱形外的不算。
        const CellCoord around[4] = {
            {base.x, static_cast<std::int16_t>(base.y - 1)},
            {static_cast<std::int16_t>(base.x - 1),
             static_cast<std::int16_t>(base.y - 1)},
            {static_cast<std::int16_t>(base.x - 1), base.y},
            base,
        };
        for (int i = 0; i < 4; ++i) {
          if (!ctx.grid.InMap(around[i])) continue;
          if (!IsRaisable(ctx, around[i])) e.locked = true;
        }
      } else {
        // 四个探测点全在菱形外：这个角点谁也碰不到，锁死、高度留 0
        e.locked = true;
      }
      e.height = height;
    }
  }
}

int GetCurrentLevel(const HillContext& ctx, const CellCoord& c) {
  const CornerLattice& lat = ctx.lattice;
  int h[4];
  CornerHeights(lat, c.x - lat.origin_x, c.y - lat.origin_y, h);
  int mn = 1000;                                  // 哨兵，四个角一定都参与比较
  for (int i = 0; i < 4; ++i) {
    if (h[i] < mn) mn = h[i];
  }
  if (IsRaisable(ctx, c)) return mn / 15;         // 有符号除法（向零截断）
  return ctx.world->Level(c);                     // 不可抬升 -> 报当前 Level
}

int PickCorners(const HillContext& ctx, const CellCoord& c, int dir) {
  const CornerLattice& lat = ctx.lattice;
  const int cx = c.x - lat.origin_x, cy = c.y - lat.origin_y;
  int h[4];
  CornerHeights(lat, cx, cy, h);
  const bool locked[4] = {
      lat.At(cx, cy).locked,
      lat.At(cx + 1, cy).locked,
      lat.At(cx + 1, cy + 1).locked,
      lat.At(cx, cy + 1).locked,
  };

  int mx = -1, mn = 1000;
  for (int i = 0; i < 4; ++i) {
    if (h[i] < mn) mn = h[i];
    if (h[i] > mx) mx = h[i];
  }

  int mask = 0;
  if (mx == mn) {                       // 四角等高：全部未锁的角一起动（常态）
    for (int i = 0; i < 4; ++i) {
      if (!locked[i]) mask += 1 << i;
    }
    return mask;
  }
  if (dir > 0) {                        // 抬：把低于最高角的角提上来
    for (int i = 0; i < 4; ++i) {
      if (h[i] < mx && !locked[i]) mask += 1 << i;
    }
    return mask;
  }
  for (int i = 0; i < 4; ++i) {         // 降：把高于最低角的角压下去
    if (h[i] > mn && !locked[i]) mask += 1 << i;
  }
  return mask;
}

bool CheckSlopes(HillContext& ctx, int dir, bool force, int x, int y) {
  CornerLattice& lat = ctx.lattice;
  const int h0 = lat.At(x, y).height;
  // dy 外层、dx 内层，各 -1..+1，跳过 (0,0)。递归按这个顺序展开，不能改。
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx == 0 && dy == 0) continue;
      const int nx = x + dx, ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= lat.stride || ny >= lat.rows) {
        if (!force) return false;       // 越出格网边界即判非法
        continue;
      }
      CornerEntry& e = lat.At(nx, ny);
      const int d0 = e.height - h0;
      const int ad0 = d0 < 0 ? -d0 : d0;
      if (e.locked && !(force && e.byte6)) {
        if (ad0 > 15) return false;     // 锁住的角只能验，不能改
        continue;
      }
      if (ad0 <= 15) continue;
      // 连带调整**不夹** [0, 180]，理论上能把角点推到负数或超过 180
      if (dir == 1) {
        if (e.height < h0) {
          lat.undo.push_back(CornerUndo{nx, ny, e.height});
          e.height = h0 - 15;
          e.dirty = true;
        }
      } else if (e.height > h0) {
        lat.undo.push_back(CornerUndo{nx, ny, e.height});
        e.height = h0 + 15;
        e.dirty = true;
      }
      // 方向不对（没真改高度）时也照样递归
      if (!e.locked && !CheckSlopes(ctx, dir, force, nx, ny)) {
        if (!force) return false;
      }
    }
  }
  return true;
}

namespace {
// 0x6B4039 / 0x6B407C 那两段一模一样的回滚：倒序还原高度，**只写 +0x00**，
// dirty 保持 1；随后整个数组被 free、ActiveCount 清零。
void RollbackUndo(CornerLattice& lat) {
  for (int i = static_cast<int>(lat.undo.size()) - 1; i >= 0; --i) {
    CornerEntry& e = lat.At(lat.undo[i].x, lat.undo[i].y);
    e.height = lat.undo[i].old_height;
  }
  lat.undo.clear();
}
}  // namespace

bool RaiseCorners(HillContext& ctx, int dir, bool force, const CellCoord& c,
                  int mask) {
  CornerLattice& lat = ctx.lattice;
  // 0x6B3E7A 起：一进来就把整个撤销数组 free 掉，撤销范围只限本次调用
  lat.undo.clear();

  // 掩码从环形角序（北东南西）重排成行优先序：bit2 与 bit3 互换
  const int m = ((mask >> 1) & 4) | ((mask & 4) << 1) | (mask & 3);

  const int cx = c.x - lat.origin_x, cy = c.y - lat.origin_y;
  int bit = 1;
  for (int y = cy; y < cy + 2; ++y) {
    for (int x = cx; x < cx + 2; ++x, bit <<= 1) {
      // bit <<= 1 在内层末尾**无条件**执行，没被选中的角也消耗一个位
      if ((m & bit) == 0) continue;
      CornerEntry& e = lat.At(x, y);
      if (e.locked) {
        if (force) continue;
        RollbackUndo(lat);              // 撞到锁住的角就整批撤销
        return false;
      }
      const int nh = e.height + dir * 15;
      e.height = nh;
      if (nh < 0 || nh > 0xB4) {
        // 撞到 [0, 180] 边界：高度原样退回，且**不入撤销栈**
        e.height = nh - dir * 15;
      } else {
        lat.undo.push_back(CornerUndo{x, y, nh - dir * 15});
      }
      // 退回那条路也会走到这里：dirty 照置、CheckSlopes 照跑。
      // 取证 0x6B3FE6 `sub eax,ecx / mov [esi],eax / jmp 0x6B3FC3`，
      // 落点就在 0x6B3FCF `mov byte [esi+5],1` 之前。
      e.dirty = true;
      if (!CheckSlopes(ctx, dir, force, x, y) && !force) {
        RollbackUndo(lat);
        return false;
      }
    }
  }
  return true;
}

void ApplyLatticeToCells(HillContext& ctx) {
  CornerLattice& lat = ctx.lattice;
  HillWorld& world = *ctx.world;
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    const int cx = c.x - lat.origin_x, cy = c.y - lat.origin_y;
    if (!lat.At(cx, cy).dirty && !lat.At(cx + 1, cy).dirty &&
        !lat.At(cx + 1, cy + 1).dirty && !lat.At(cx, cy + 1).dirty) {
      return;
    }
    int h[4];
    CornerHeights(lat, cx, cy, h);
    int mn = 1000, mx = 0;              // mx 初值是 0，不是 -1（0x6B4240 才是 -1）
    for (int i = 0; i < 4; ++i) {
      if (h[i] < mn) mn = h[i];
      if (h[i] > mx) mx = h[i];
    }
    if (mx - mn >= 0x10) return;        // 高差超过一级：这格放弃，砖号原样
    if (!IsRaisable(ctx, c)) return;

    world.SetLevel(c, static_cast<signed char>(mn / 15));
    for (int i = 0; i < 4; ++i) h[i] -= mn;
    for (int k = 0; k < 19; ++k) {
      if (kRampTable[k][0] == h[0] && kRampTable[k][1] == h[1] &&
          kRampTable[k][2] == h[2] && kRampTable[k][3] == h[3]) {
        world.SetRamp(c, k);
        // 0x6B3A29-0x6B3A44：k == 0（平地）写 [0xAA10B0]（运行时值 0 =
        // 草地，**不是** ini 的 ClearTile=29）；k != 0 写 ramp_base + k - 1。
        // CLI 曾误用 clear_tile，造山会涂出一大片 29-32 的砖，游戏里没有。
        world.SetIsoTile(c, k == 0 ? 0 : ctx.theater->ramp_base - 1 + k);
        break;
      }
      // 查不到就什么都不做：+0x11C 与 +0x38 保留原值，+0x11B 却已经改了
    }
  });
  lat.cells.clear();                    // free([0xB0B6EC])；格网用完即销毁
  lat.undo.clear();
}

void FlattenRampBlocks(HillContext& ctx) {
  HillWorld& world = *ctx.world;
  // 0x5A36DE 起摆在栈上的四个增量，顺序 (0,0) (1,0) (1,1) (0,1)
  static const CellCoord kOff[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  // 0x5A3716 起的四个立即数：另一种拼法要求的坡面编号
  static const int kGroupB[4] = {0x0B, 0x0C, 0x09, 0x0A};

  ctx.grid.ForEachCell([&](const CellCoord& c) {
    const int ramp = static_cast<signed char>(world.Ramp(c));
    int bump = 0;
    bool ok = true;
    if (ramp == 5) {
      for (int i = 0; i < 4; ++i) {
        const HillRef r = LookupCell(world, c.x + kOff[i].x, c.y + kOff[i].y);
        if (RampOf(world, r) != 5 + i) ok = false;
      }
    } else if (ramp == 0x0B) {
      bump = 1;
      for (int i = 0; i < 4; ++i) {
        const HillRef r = LookupCell(world, c.x + kOff[i].x, c.y + kOff[i].y);
        if (RampOf(world, r) != kGroupB[i]) ok = false;
      }
    } else {
      return;                           // 两种起手形状之外，整格跳过
    }
    if (!ok) return;
    for (int i = 0; i < 4; ++i) {
      // 第二遍重新查一次格子——哑元的坐标会被再写一次，照抄
      const HillRef r = LookupCell(world, c.x + kOff[i].x, c.y + kOff[i].y);
      if (r.dummy) {
        // 哑元的 +0x38 / +0x11A / +0x11B 三写合一
        world.SetDummyTile(0xFFFF, 0, world.DummyLevel() + bump);
        world.SetDummyRamp(0);
      } else {
        world.SetIsoTile(r.c, 0xFFFF);
        world.SetSubTile(r.c, 0);
        world.SetRamp(r.c, 0);
        world.SetLevel(r.c, world.Level(r.c) + bump);
      }
    }
  });
}

void RaiseAllCells(HillContext& ctx) {
  // 0x5A361A 起按**线性下标**扫工作数组，不是按格子迭代器。
  const int n = ctx.grid.side() * ctx.grid.side();
  for (int i = 0; i < n; ++i) {
    PaintWorkCell& w = (*ctx.work)[static_cast<std::size_t>(i)];
    // 打包坐标 (0,0) 的槽位（菱形外）整条跳过
    if (w.coord.x == 0 && w.coord.y == 0) continue;
    const CellCoord c = w.coord;
    // 0x5A3652：fild 的是**有符号字节**的层高，加上工作记录里那个已经
    // 四舍五入成整数的高度场
    const double target = static_cast<double>(ctx.world->Level(c)) + w.height;
    const int level = GetCurrentLevel(ctx, c);
    const int steps = Ftol(target - static_cast<double>(level));
    // 0x5A367A 起：setge / dec / and -2 / inc，把符号压成 +-1（0 也给 +1）
    const int dir = steps >= 0 ? 1 : -1;
    for (int k = steps < 0 ? -steps : steps; k > 0; --k) {
      // mask 每一级都重算（回跳点 0x5A368E 在 call 0x6B4240 之前）
      const int mask = PickCorners(ctx, c, dir);
      // 返回值被完全忽略：失败时已整批回滚，剩下的级数照抬不误
      RaiseCorners(ctx, dir, false, c, mask);
    }
  }
}

void CreateHills(HillContext& ctx) {
  MarkHeightAnchors(*ctx.world, ctx.grid, *ctx.theater, *ctx.work);     // (1)
  GenerateHeightField(ctx.grid, *ctx.work, ctx.ruggedness, *ctx.nd);    // (2)
  BuildCornerLattice(ctx);                                             // (3)
  RaiseAllCells(ctx);                                                  // (4)
  ApplyLatticeToCells(ctx);                                            // (5)
  FlattenRampBlocks(ctx);                                              // (6)
}

}  // namespace rmg
}  // namespace ra2
