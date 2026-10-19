// 死代码复现 —— 0x5A6D80 / 0x5A7070 定向步进（batch 930）
//
// 语义链见 dead_code_stepdir.h 头注释。实现要点（全部有汇编依据 + emu 黄金
// 实证，golden 5 用例：deadcode_stepdir_golden.txt）：
//  0x5A6D80：档位方向向量（==2：(1,3)+off1c=-3/off18=6；!=2：(3,1)+ebp=-3/
//    off14=6）→ 主循环判界 X>=w+x / Y>=h+y 出界写 0 → GetCellAt(cell+0x38)
//    ==[0xABC2B0] 命中分支，否则方向表步进 → 命中分支组装矩形
//    {X+ebp, Y+off1c, edi+off14} 调 ProbeArea（0x5A7250 桩）→ 成功走
//    0x5A6F55 距离采样（max(Y-refY,X-refX)，拒绝采样 n=ftol(rand*(esi+1)*
//    2^-32) 无符号 ja 重抽）→ n>0 反向补步（(direction-4)&7）逐格组装
//    {X'+ebp, Y'+[esp+0x20], edi, ebx} 调 ProbeArea → 成功写最终坐标。
//  0x5A7070：判界带 +1 余量（X>=w+x+1）→ 每步组装矩形调 ProbeArea →
//    成功方向表步进 / 失败 0x5A7192 补步：目标=当前+反向方向，距离
//    =max(目标-ref)，<=6 直写不吃 RNG；>6 拒绝采样 n=ftol(rand*4/2^32)
//    （cmp 3 ja 重抽）→ n>0 沿反向逐格补 n 步写坐标。
#include "dead_code_stepdir.h"

#include <cmath>
#include <cstdint>

namespace ra2 {
namespace rmg {
namespace {

// .rdata 取证（bytes 见头注释）：
constexpr double kTwo32Inv = 2.3283064370807974e-10;  // [0x7ED898] ≈ 2^-32
constexpr double kFourInv32 = 9.31322574832319e-10;   // [0x7EDA38] ≈ 4/2^32

inline int Ftol(double v) { return static_cast<int>(v); }

inline std::uint32_t Pack(int x, int y) {
  return (static_cast<std::uint32_t>(static_cast<std::uint16_t>(x))) |
         (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16);
}

// 方向表步进（0x5A6E41/0x5A6EF6）：direction&7 查表，坐标 += (dx,dy)
inline void Step8(Dir8Entry const* dir8, int dir, int& x, int& y) {
  const Dir8Entry& e = dir8[dir & 7];
  x += e.dx;
  y += e.dy;
}

}  // namespace

std::uint32_t DeadCodeStepDir6D80(const DeadStepDirContext& ctx,
                                  int refx, int refy, CellCoord begin,
                                  std::int32_t* out_al) {
  Randomizer& rng = *ctx.rng;
  DeadStepDirWorld& world = *ctx.world;
  const int w = ctx.region.w, h = ctx.region.h;
  const int rx = ctx.region.x, ry = ctx.region.y;
  const int direction = ctx.direction;
  const Dir8Entry* dir8 = ctx.dir8;

  // 档位（0x5A6DB0-0x5A6DFF）：==2 走 (1,3)；!=2 走 (3,1)
  int edi = 0, ebx = 0, ebp = 0;
  int off1c = 0, off18 = 0, off14 = 0;
  if (direction == 2) {
    edi = 1; ebx = 3; off1c = -3; off18 = 6;   // ebp=0, off14=0
  } else {
    edi = 3; ebx = 1; ebp = -3; off14 = 6;     // off1c=0, off18=0
  }

  int x = begin.x;   // 初始坐标 = 参考坐标（[esp+0x10] 打包）
  int y = begin.y;

  // 主循环（0x5A6E01）：判界 → GetCellAt → 命中分支 / 方向表步进
  for (;;) {
    if (x >= w + rx || y >= h + ry) {
      if (out_al) *out_al = 0;   // 0x5A6E79/0x5A6F35：写 0
      return 0;
    }
    // 命中分支（0x5A6E99 从 GetCellAt 进入，带命中检测）
    const std::uint32_t cell38 = world.CellOwner(x, y);
    if (cell38 == ctx.abc_b2b0) {
      // ★命中分支内部循环（0x5A6E9D 判界 → 组装 → ProbeArea）：
      //   判定失败走 0x5A6EF6 方向表步进后 jmp 0x5A6E9D **复用判界**，
      //   不再回主循环开头重新 GetCellAt（golden 6d80-hit-fail 实证：
      //   首拒后第二格 ProbeArea 矩形 a=1 即步进到 (1,0) 继续命中分支）
      for (;;) {
        // 命中分支判界（0x5A6E9D-0x5A6EBC）
        if (x >= w + rx || y >= h + ry) {
          if (out_al) *out_al = 0;
          return 0;
        }
        // 组装矩形 {X+ebp, Y+off1c, edi+off14}（0x5A6EBE-0x5A6EE4）
        const int rect_a = x + ebp;
        const int rect_b = y + off1c;
        const int rect_c = edi + off14;
        if (world.ProbeArea(rect_a, rect_b, rect_c, 0)) {
          // 成功路径（0x5A6F55）：距离 = max(Y-refY, X-refX)
          int esi = x - refx;
          int eax = y - refy;
          if (eax > esi) esi = eax;
          if (esi <= 0) {
            // 0x5A7060：写当前坐标返回（hit-ok：起点=参考点距离 0）
            if (out_al) *out_al = 1;
            return Pack(x, y);
          }
          // 拒绝采样 n ∈ [0, esi]（0x5A6F7E-0x5A6FC0，ja 无符号）
          int n = 0;
          for (;;) {
            const std::uint32_t r = rng.Random();
            n = Ftol(static_cast<double>(r) *
                     static_cast<double>(esi + 1) * kTwo32Inv);
            if (static_cast<std::uint32_t>(n) <=
                static_cast<std::uint32_t>(esi)) break;
          }
          if (n <= 0) {
            // 0x5A705C：写当前坐标返回
            if (out_al) *out_al = 1;
            return Pack(x, y);
          }
          // 反向补步（0x5A6FCE-0x5A704A）：(direction-4)&7 方向逐格
          edi += off14;
          ebx += off18;
          const int back = (direction - 4) & 7;
          for (int i = 0; i < n; ++i) {
            const Dir8Entry& e = dir8[back];
            x += e.dx;
            y += e.dy;
            // 每步组装矩形 {X+ebp, Y+[esp+0x20](i+1), edi, ebx} 调 0x5A7250
            if (!world.ProbeArea(x + ebp, y + (i + 1), edi, ebx)) {
              // 0x5A705C：中止，写当前坐标
              if (out_al) *out_al = 1;
              return Pack(x, y);
            }
          }
          // 0x5A704C：写最终坐标
          if (out_al) *out_al = 1;
          return Pack(x, y);
        }
        // 0x5A6EF6：命中判定失败 → 方向表步进后回 0x5A6E9D 复用判界
        Step8(dir8, direction, x, y);
      }
    }
    // 未命中：方向表步进（0x5A6E41-0x5A6E77）→ 主循环
    Step8(dir8, direction, x, y);
  }
}

std::uint32_t DeadCodeStepDir7070(const DeadStepDirContext& ctx,
                                  int refx, int refy, CellCoord begin,
                                  std::int32_t* out_al) {
  Randomizer& rng = *ctx.rng;
  DeadStepDirWorld& world = *ctx.world;
  const int w = ctx.region.w, h = ctx.region.h;
  const int rx = ctx.region.x, ry = ctx.region.y;
  const int direction = ctx.direction;
  const Dir8Entry* dir8 = ctx.dir8;

  // 档位（0x5A70A4-0x5A70E6）：==2 → (esi,edi)=(1,3)、off14=-3、off1c=6
  //   !=2 → (3,1)、off10=-3、off18=6
  int esi0 = 0, edi0 = 0;
  int off10 = 0, off14 = 0, off18 = 0, off1c = 0;
  if (direction == 2) {
    esi0 = 1; edi0 = 3; off14 = -3; off1c = 6;
  } else {
    esi0 = 3; edi0 = 1; off10 = -3; off18 = 6;
  }

  int x = begin.x;
  int y = begin.y;

  // 主循环（0x5A70EE）：判界带 +1 → 每步组装矩形调 ProbeArea
  for (;;) {
    if (x >= w + rx + 1 || y >= h + ry + 1) {
      break;   // 出界 → 0x5A7192 补步
    }
    // 组装矩形 {X+offxx, Y+offyy, esi0+offxx, edi0+offyy}
    const int rect_a = x + off10;
    const int rect_b = y + off14;
    const int rect_c = esi0 + off18;
    const int rect_d = edi0 + off1c;
    if (!world.ProbeArea(rect_a, rect_b, rect_c, rect_d)) {
      break;   // 0x5A7153 je 0x5A7192
    }
    // 0x5A7155：方向表 (direction&7) 步进 → 主循环
    Step8(dir8, direction, x, y);
  }

  // 补步分支（0x5A7192）：反向方向 idx=(direction-4)&7
  const int back = (direction - 4) & 7;
  const Dir8Entry& be = dir8[back];
  int tx = x + be.dx;
  int ty = y + be.dy;
  // 距离 = max(目标X-refX, 目标Y-refY)
  int dax = tx - refx;
  int day = ty - refy;
  int dist = (dax > day) ? dax : day;
  if (dist <= 6) {
    // 0x5A71E6 jle 0x5A7239：直写目标（不吃 RNG）
    if (out_al) *out_al = 1;
    return Pack(tx, ty);
  }
  // 拒绝采样 n ∈ [0,3]（0x5A71E8-0x5A720E，[0x7EDA38]=4/2^32）
  int n = 0;
  for (;;) {
    const std::uint32_t r = rng.Random();
    n = Ftol(static_cast<double>(r) * kFourInv32);
    if (static_cast<std::uint32_t>(n) <= 3u) break;
  }
  if (n <= 0) {
    if (out_al) *out_al = 1;
    return Pack(tx, ty);
  }
  // 0x5A7214：沿反向方向逐格补 n 步
  for (int i = 0; i < n; ++i) {
    tx += be.dx;
    ty += be.dy;
  }
  if (out_al) *out_al = 1;
  return Pack(tx, ty);
}

}  // namespace rmg
}  // namespace ra2