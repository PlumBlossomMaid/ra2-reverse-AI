// 死代码复现 —— 0x5A7440 菱形域矩形高度一致性校验（batch 935）
//
// 语义链见 dead_code_7440.h 头注释。实现要点（全部有汇编依据 + emu 黄金
// 实证，golden 10 用例：deadcode_7440_golden.txt）：
//   · 四角菱形判定（0x5A7449-0x5A755F）：角 0=(x,y)、角 1=(x+w-1,y)、
//     角 2=(x,y+h-1)、角 3=(x+w-1,y+h-1)。每角四判据（判据顺序固定）：
//       X+Y <= W → fail（[0xABED04]）；X-Y >= W → fail；
//       Y-X >= W → fail；X+Y > W+2H → fail（[0xABED08]）。
//     ★角坐标 16 位截断：角 1/3 的 X 与角 2/3 的 Y 是 32 位 lea 求值后
//     movsx 截断回 16 位（0x5A7497/0x5A74DF），与 0x5A7250同构。
//   · 起点格：GetCellAt((x,y))（0x5A7579，无条件）→ [cell+0x11B] movsx
//     有符号字节 → 期望高度（存 [esp+0x1C]）。
//   · 全矩形扫描（外层 cy、内层 cx，x 优先）：
//       外层条件 cy < h+raw_y（0x5A7591 cmp；jge 0x5A762C → 成功返 1，
//       但★起点格已查——golden h-zero gc=1 实证）；
//       内层条件 cx < w+raw_x（0x5A75A0 cmp；jge 0x5A761B → 外层递增，
//       golden w-zero gc=1 实证，四角 + 起点格仍查）。
//       每格：CellHeight(cx,cy) vs 期望高度（0x5A75C3 movsx
//       edx,[cell+0x11B]; 0x5A75CA cmp; jne 0x5A7636 fail——高度不等
//       立即失败，不查谓词）→ tile → 5 谓词链按序：
//         a. IsPaved0（0x4866D0，[paved, +0xf)）：过 → flagA!=0 → 继续
//            （0x5A760F）；flagA==0 → 整体 fail（0x5A760D je）
//         b. IsPaved4（0x4866F0，[paved4, +4)）：过 → ★共用 flagA 检查
//         c. IsVoidTile（0x486380，==0xFFFF||==0）：过 → 直接继续
//            （0x5A75ED jne，不查 flag）
//         d. IsMiscPaveTile（0x486650，[misc, +0xE)）：过 → 直接继续
//         e. IsPaveTile（0x486670，[pavet, +0x10)）：过 → 直接继续；
//            全失败 → 整体 fail（0x5A7603 je）
//   · 全过 → 0x5A762F mov al,1 返回 1。RNG 不参与。
//
// 原版怪癖（golden 实证，照抄）：
//   ★① 高度先于谓词：扫描格高度 != 起点格高度 → 立即 fail（不查谓词）。
//      height-mismatch：gc=7（起点1+扫描6）但 p6d0=5——末格(10,7) 高度
//      9≠7 在谓词链前就被拦下。
//   ★② 双前置谓词门控共用 dl 单标志：6D0/6F0 通过后查 flagA，为 0 时
//      整体失败——flagA-zero gc=2 实证首格即 fail（起点1+首扫描格1，
//      后续格未访问）。
//   ③ 起点格无条件先查：h-zero/w-zero gc=1（矩形空但起点格照查）。
//   ④ 谓词链序：6D0 → 6F0 → 380 → 650 → 670，前两个带 flag 门控。
//   ⑤ 角 1/3、2/3 的加宽坐标先 32 位求值再截断回 16 位（movsx）。
//   ⑥ 谓词/GetCellAt 全是有符号比较；tile 按有符号 int32 读、cell+0x11B
//     高度按有符号字节（movsx）读。
#include "dead_code_7440.h"

#include <cstdint>

namespace ra2 {
namespace rmg {

int DeadCode7440(const DeadCode7440Context& ctx) {
  DeadCode7440World& world = *ctx.world;
  const int W = ctx.w_bound;         // [0xABED04]
  const int W2H = ctx.w2h_bound;     // [0xABED08]

  // 角坐标（16 位截断；raw_x/raw_y 高 16 位 golden 恒 0）：
  //   角 0/2 的 X、角 0/1 的 Y：movsx（低 16 符号扩展）
  const int x16 = static_cast<std::int16_t>(ctx.raw_x & 0xFFFF);
  const int y16 = static_cast<std::int16_t>(ctx.raw_y & 0xFFFF);
  //   角 1/3 的 X：lea edx,[w+ecx-1]（32 位）→ movsx dx（截断 16）
  const int xw1 = static_cast<std::int16_t>(
      static_cast<std::uint32_t>(ctx.w) + (ctx.raw_x & 0xFFFF) - 1);
  //   角 2/3 的 Y：lea edx,[h+edi-1]（32 位）→ movsx dx（截断 16）
  const int yh1 = static_cast<std::int16_t>(
      static_cast<std::uint32_t>(ctx.h) + (ctx.raw_y & 0xFFFF) - 1);

  // 单角四判据（0x5A7460-0x5A751E 顺序：X+Y<=W / X-Y>=W / Y-X>=W /
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

  // 期望高度：起点格 GetCellAt((x,y)) 的 cell+0x11B
  // （0x5A7565-0x5A7584：lea CellStruct → push → mov [esp+0x18],si →
  // mov [esp+0x1A],bx → call 0x5657A0 → movsx ecx,[eax+0x11B]）
  const std::int8_t expected = world.CellHeight(x16, y16);

  // 全矩形扫描（0x5A757E-0x5A7626）：外层 cy、内层 cx（x 优先）
  for (int cy = y16; cy < ctx.h + y16; ++cy) {
    for (int cx = x16; cx < ctx.w + x16; ++cx) {
      // ★高度一致性比对（0x5A75C3 movsx edx,[cell+0x11B]; 0x5A75CA
      // cmp edx,eax; 0x5A75CC jne 0x5A7636 —— 高度不等直接整体 fail）
      if (world.CellHeight(cx, cy) != expected) {
        return 0;
      }
      const std::int32_t tile = world.CellTile(cx, cy);
      // a. 0x4866D0：[paved, paved+0xf)，通过后门控 flagA
      if (world.IsPaved0(tile)) {
        if (ctx.flag_a != 0) continue;   // 0x5A7605 jmp 0x5A760F
        return 0;                        // 0x5A760D je 0x5A7636
      }
      // b. 0x4866F0：[paved4, paved4+4)，★与 6D0 共用 dl 标志
      if (world.IsPaved4(tile)) {
        if (ctx.flag_a != 0) continue;   // 同上路径
        return 0;
      }
      // c. 0x486380：==0xFFFF || ==0（直接继续，0x5A75ED jne 0x5A760F）
      if (world.IsVoidTile(tile)) continue;
      // d. 0x486650：[misc, misc+0xE)（直接继续，0x5A75F8 jne）
      if (world.IsMiscPaveTile(tile)) continue;
      // e. 0x486670：[pavet, pavet+0x10)（0x5A75FC call；0x5A7603 je
      //    0x5A7636 —— 全失败 → fail；命中 → 落 0x5A7605 继续）
      if (world.IsPaveTile(tile)) continue;
      return 0;
    }
  }

  return 1;   // 0x5A762F mov al,1
}

}  // namespace rmg
}  // namespace ra2