// 死代码复现 —— 0x5A76E0 菱形域矩形完整落点谓词（batch 932）
//
// 语义链见 dead_code_76e0.h 头注释。实现要点（全部有汇编依据 + emu 黄金
// 实证，golden 10 用例：deadcode_76e0_golden.txt）：
//   - 四角菱形判定（0x5A76E9-0x5A77FA）：角 0=(x,y)、角 1=(x+w-1,y)、
//     角 2=(x,y+h-1)、角 3=(x+w-1,y+h-1)。每角四判据（判据顺序固定）：
//       X+Y <= W → fail（[0xABED04]）；X-Y >= W → fail；
//       Y-X >= W → fail；X+Y > W+2H → fail（[0xABED08]）。
//     ★角坐标 16 位截断：角 1/3 的 X 与角 2/3 的 Y 是 32 位求值后
//     movsx 截断回 16 位（0x5A773B/0x5A7786/0x5A77C0-0x5A77C5），
//     角 0/2 的 X、角 0/1 的 Y 直接 (int16)raw_x/y。
//   - 内块扫描（0x5A7800-0x5A7859，3×3）：外层 y、内层 x（x 优先，
//     golden inner-fail gc=5 实锤遍历序）。每格 CellTile → tile：
//       tile ∈ [paved, paved+1]（闭区间，0x5A782C-0x5A7839 cmp/jl/inc/
//       jle）→ 直接过；否则 0x486730（[medians, medians+14)）。
//   - 边带扫描（0x5A785B-0x5A78AE，3 格 (x..x+2, y+3)）：每格 CellTile
//     → tile，按序三谓词任一通过即继续，全失败 → 返回 0：
//       0x486670（[pavet, pavet+16)）→ 0x486380（tile==0xFFFF||0）
//       → 0x486650（[misc, misc+14)）。
//   - 全过 → 返回 1。RNG 不参与。
//
// 谓词/GetCellAt 全部跑真身（golden hook 只计数），C++ 侧 Fake 用同一
// 窗口语义（有符号 jl/jge 半开区间 / ==0xFFFF||==0）并计数对拍。
#include "dead_code_76e0.h"

#include <cstdint>

namespace ra2 {
namespace rmg {

int DeadCode76E0(const DeadCode76E0Context& ctx) {
  DeadCode76E0World& world = *ctx.world;
  const int W = ctx.w_bound;         // [0xABED04]
  const int W2H = ctx.w2h_bound;     // [0xABED08]

  // 角坐标（16 位截断；raw_x/raw_y 高 16 位 golden 恒 0）：
  //   角 0/2 的 X、角 0/1 的 Y：movsx cx/di（低 16 符号扩展）
  const int x16 = static_cast<std::int16_t>(ctx.raw_x & 0xFFFF);
  const int y16 = static_cast<std::int16_t>(ctx.raw_y & 0xFFFF);
  //   角 1/3 的 X：lea edx,[w+ecx-1]（32 位）→ movsx dx（截断 16）
  const int xw1 = static_cast<std::int16_t>(
      static_cast<std::uint32_t>(ctx.w) + (ctx.raw_x & 0xFFFF) - 1);
  //   角 2/3 的 Y：lea edx,[h+edi-1]（32 位）→ movsx dx（截断 16）
  const int yh1 = static_cast<std::int16_t>(
      static_cast<std::uint32_t>(ctx.h) + (ctx.raw_y & 0xFFFF) - 1);

  // 单角四判据（0x5A76F9-0x5A772A 顺序：X+Y<=W / X-Y>=W / Y-X>=W /
  // X+Y>W+2H；全部有符号 jle/jge/jg）
  const auto corner_fail = [W, W2H](int X, int Y) -> bool {
    if (X + Y <= W) return true;
    if (X - Y >= W) return true;
    if (Y - X >= W) return true;
    if (X + Y > W2H) return true;
    return false;
  };
  if (corner_fail(x16, y16)) return 0;    // 角 0 (x, y)
  if (corner_fail(xw1, y16)) return 0;    // 角 1 (x+w-1, y)
  if (corner_fail(x16, yh1)) return 0;    // 角 2 (x, y+h-1)
  if (corner_fail(xw1, yh1)) return 0;    // 角 3 (x+w-1, y+h-1)

  // 内块 3×3（0x5A7800-0x5A7859）：外层 y、内层 x（x 优先）
  for (int cy = y16; cy < y16 + 3; ++cy) {
    for (int cx = x16; cx < x16 + 3; ++cx) {
      const std::int32_t tile = world.CellTile(cx, cy);
      // 闭区间 [paved, paved+1]（0x5A782C jl → 0x5A7839 jle 直通）
      if (tile >= ctx.paved && tile <= ctx.paved + 1) continue;
      // 0x486730：[medians, medians+14)
      if (!world.IsMedianTile(tile)) return 0;
    }
  }

  // 边带 3 格 (x..x+2, y+3)（0x5A785B-0x5A78AE）
  for (int cx = x16; cx < x16 + 3; ++cx) {
    const std::int32_t tile = world.CellTile(cx, y16 + 3);
    // 0x486670：[pavet, pavet+16)
    if (world.IsPaveTile(tile)) continue;
    // 0x486380：tile == 0xFFFF || tile == 0
    if (world.IsVoidTile(tile)) continue;
    // 0x486650：[misc, misc+14)
    if (world.IsMiscPaveTile(tile)) continue;
    return 0;
  }

  return 1;   // 0x5A78B3 mov al,1
}

}  // namespace rmg
}  // namespace ra2