// 死代码复现 —— 0x5A7250 菱形域矩形扫描校验（batch 934）
//
// 语义链见 dead_code_7250.h 头注释。实现要点（全部有汇编依据 + emu 黄金
// 实证，golden 14 用例：deadcode_7250_golden.txt）：
//   · 四角菱形判定（0x5A7262-0x5A7377）：角 0=(x,y)、角 1=(x+w-1,y)、
//     角 2=(x,y+h-1)、角 3=(x+w-1,y+h-1)。每角四判据（判据顺序固定）：
//       X+Y <= W → fail（[0xABED04]）；X-Y >= W → fail；
//       Y-X >= W → fail；X+Y > W+2H → fail（[0xABED08]）。
//     ★角坐标 16 位截断：角 1/3 的 X 与角 2/3 的 Y 是 32 位 lea 求值后
//     movsx 截断回 16 位（0x5A72AD/0x5A72F5/0x5A733A），与 0x5A76E0
//     （batch 932）同构。
//   · 全矩形扫描（★对比 76E0 的 3×3+边带，这里是整个 w×h 矩形）：
//     外层 cy 从 raw_y 起、条件 cy < h+raw_y（0x5A7386 add/0x5A738A
//     jge 0x5A741E：h<=0 → 直接成功返 1，跳过扫描——golden h-zero
//     gc=0 实证）；内层 cx 从 raw_x 起、条件 cx < w+raw_x
//     （0x5A7392 lea/0x5A7395 jge 0x5A740D：w<=0 → 内层空，跳到外层
//     递增——golden w-zero gc=0 实证，四角仍判）。
//   · 每格：CellTile（GetCellAt 0x5657A0 真身）→ tile → 5 谓词链按序
//     （0x5A73B6-0x5A73FD，任一通过 → 本格过；全失败 → 整体 fail）：
//       a. IsPaved0（0x4866D0，[paved, +0xf)）：过 → flagA!=0 → 继续
//          （0x5A73C7 jmp）；flagA==0 → 整体 fail（0x5A73C5 je）
//       b. IsPaved4（0x4866F0，[paved4, +4)）：过 → flagB!=0 → 继续；
//          ==0 → 整体 fail（0x5A73DA je）
//       c. IsVoidTile（0x486380，==0xFFFF||==0）：过 → 继续
//          （0x5A73E7 jne）
//       d. IsMiscPaveTile（0x486650，[misc, +0xE)）：过 → 继续
//       e. IsPaveTile（0x486670，[pavet, +0x10)）：过 → 继续；
//          全失败 → 整体 fail（0x5A73FD je）
//   · 全过 → 0x5A7421 mov al,1 返回 1。RNG 不参与。
//
// 原版怪癖（golden 实证，照抄）：
//   ① 双前置谓词门控：0x4866D0/0x4866F0 通过后还要检查 flagA/flagB，
//     flag 为 0 时整体失败（不是跳过该格）——flagA-zero/flagB-zero
//     gc=1 实证首格即 fail（其余 5 格未访问）。
//   ② 谓词链序与 76E0 边带（670→380→650）不同：7250 是
//     6D0 → 6F0 → 380 → 650 → 670。
//   ③ 扫描范围是整矩形；h<=0 / w<=0 时四角仍判、扫描区空。
//   ④ 角 1/3、2/3 的加宽坐标先 32 位求值再截断回 16 位（movsx）。
//   ⑤ 谓词/GetCellAt 全是有符号比较（jl/jge），tile 按有符号 int32 读。
#include "dead_code_7250.h"

#include <cstdint>

namespace ra2 {
namespace rmg {

int DeadCode7250(const DeadCode7250Context& ctx) {
  DeadCode7250World& world = *ctx.world;
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

  // 单角四判据（0x5A727C-0x5A72A4 顺序：X+Y<=W / X-Y>=W / Y-X>=W /
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

  // 全矩形扫描（0x5A737D-0x5A7418）：外层 cy、内层 cx（x 优先）
  for (int cy = y16; cy < ctx.h + y16; ++cy) {
    for (int cx = x16; cx < ctx.w + x16; ++cx) {
      const std::int32_t tile = world.CellTile(cx, cy);
      // a. 0x4866D0：[paved, paved+0x10)，通过后门控 flagA
      if (world.IsPaved0(tile)) {
        if (ctx.flag_a != 0) continue;   // 0x5A73C7 jmp
        return 0;                        // 0x5A73C5 je
      }
      // b. 0x4866F0：[paved4, paved4+4)，通过后门控 flagB
      if (world.IsPaved4(tile)) {
        if (ctx.flag_b != 0) continue;   // 0x5A73DC jmp
        return 0;                        // 0x5A73DA je
      }
      // c. 0x486380：==0xFFFF || ==0
      if (world.IsVoidTile(tile)) continue;   // 0x5A73E7 jne
      // d. 0x486650：[misc, misc+0xE)
      if (world.IsMiscPaveTile(tile)) continue;  // 0x5A73F2 jne
      // e. 0x486670：[pavet, pavet+0x10)；全失败 → fail
      if (world.IsPaveTile(tile)) continue;      // 0x5A73FB jne
      return 0;                                  // 0x5A73FD je
    }
  }

  return 1;   // 0x5A7421 mov al,1
}

}  // namespace rmg
}  // namespace ra2