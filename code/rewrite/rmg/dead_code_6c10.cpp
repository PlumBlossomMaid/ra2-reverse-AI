// 死代码复现 —— 0x5A6C10 [0xA8ED2C] 表查询放置（batch 933）
//
// 语义链见 dead_code_6c10.h 头注释。实现要点（全部有汇编依据 + emu 黄金
// 实证，golden 10 用例：deadcode_6c10_golden.txt）：
//   · 入口 ret 8 = 2 栈参：栈参1 = [esp+4]（work 写入值，0x5A6D30 mov
//     edx,[esp+0x28]——push esi/edi 后偏移 +8）、栈参2 = [esp+8]（层高
//     调整量，0x5A6CF0 mov ecx,[esp+0x2C]，== -1 跳过 cell+0x11B）。
//     ecx = 表索引、edx = &源坐标 CellStruct（+0=X int16、+2=Y int16）。
//   · 表项 = [0xA8ED2C][index]（dword 指针数组，0x5A6C1E mov ebp,
//     [eax+ecx*4]）。
//   · 表项虚表槽 0x9C（0x5A6C23 mov edx,[ebp]; 0x5A6C26 call [edx+0x9C]）
//     → 数据表基址 → 0 → 0x5A6C32 je 返回。
//   · 外层计数 [ebp+0x2E8]（0x5A6C38/0x5A6D59）、内层计数 [ebp+0x2E4]
//     （0x5A6C54/0x5A6D4A）。任一 <=0 → 0x5A6C46/0x5A6C5E jle 返回。
//   · 双层循环（0x5A6C54 内层 / 0x5A6D59 外层）：x 优先（golden GC 序
//     (8,6)(9,6)(10,6)(8,7)... 实锤）：
//       X = (int16)(源X + 内层索引 edi)   ← 0x5A6C6B/0x5A6C72 ax=源X+
//          di
//       Y = (int16)(源Y + 外层索引 ebx)   ← 0x5A6C68/0x5A6C6E cx=ebx+
//          源Y
//       菱形四判定（0x5A6C8D-0x5A6CB4，W=[0xABED04]、W2H=[0xABED08]）：
//         X+Y<=W → 跳过（jle）；X-Y>=W → 跳过（jge）；Y-X>=W → 跳过
//         （jge）；X+Y>W2H → 跳过（jg）
//       GetCellAt(0x5657A0)（thiscall ecx=0x87F7E8）→ cell
//       索引串 = 外层索引×内层计数 + 内层索引（0x5A6CC9 ecx=内层计数、
//         0x5A6CCF ebx=外层索引、0x5A6CD7 imul ecx,ebx、0x5A6CDA add
//         ecx,edi——★先 imul 用外层索引做乘数）
//       数据项 = 数据表[索引串*4 + 0x10]（0x5A6CDC mov esi,
//         [edx+ecx*4+0x10]，edx = 槽 0x9C 返回值）
//       数据项非 0（0x5A6CE2 je 跳过写 cell 与 work）：
//         cell+0x11A = 索引串低 8 位（cl，0x5A6CEA）
//         cell+0x38  = 表项+0x294（0x5A6CE4/0x5A6CF4）
//         layer != -1：cell+0x11B = 数据项+0x28 + layer - 4
//           （0x5A6CFC-0x5A6D04，★sub dl,4）
//         cell+0x11C = 数据项+0x2A（0x5A6D0A-0x5A6D0D）
//         [0xABED10]（work）非 0：work[(Y*side+X)*0x50 + 0x38] = 栈参1
//           （0x5A6D13-0x5A6D3A：movsx Y×side + X → lea *5 → shl 4 =
//           *80=WORK_SIZE）
//   · 返回 EAX 无意义（0x5A6D6C 弹栈后 ret 8），返回 0 对拍。
//
// 原版怪癖（golden 实证，照抄）：
//   ① 索引串乘法方向：imul ecx,ebx 时 ecx=内层计数、ebx=外层索引 →
//      索引串 = 外层×内层 + 内层（非内层×外层+外层）——c11a 序列
//      all-pass 0..5、partial-fail 0..9 / 16..26 实锤。
//   ② X-Y>=W 与 Y-X>=W 是两个独立判据（0x5A6C9A/0x5A6CA4），partial-
//      fail gc=21 实锤 j=0 行 X=18 即被 X-Y>=W 拦截（X+Y<=28 尚有余量）。
//   ③ 数据项 0 时 GetCellAt 照调（gc=6）但 cell 与 work 全部跳过——
//      data-zero 用例 gc=6 cell=5 work=5 实锤（0x5A6CE2 je 跳到
//      0x5A6D3E，work 块也在其路径上）。
//   ④ layer=-1 只跳过 cell+0x11B（0x5A6CF7 cmp ecx,-1 je 0x5A6D0A），
//      cell+0x11C 照写——layer-m1 用例 CELL 8 6 4660 0 0 42 实锤。
//   ⑤ 每个表项独立虚表槽 0x9C（golden 逐表项打桩返回各自数据表——
//      index-1 用例查第二表项取自己的数据表与 c294=0x5678 实锤）。
#include "dead_code_6c10.h"

#include <cstdint>

namespace ra2 {
namespace rmg {

int DeadCode6C10(const DeadCode6C10Context& ctx) {
  DeadCode6C10World& world = *ctx.world;

  // 表项（[0xA8ED2C][index]）
  if (!ctx.table || ctx.index < 0 ||
      static_cast<std::size_t>(ctx.index) >= ctx.table->size()) {
    return 0;
  }
  const DeadCode6C10TableItem& item = (*ctx.table)[ctx.index];

  // 表项虚表槽 0x9C → 数据表基址；返回 0 → 0x5A6C32 je
  if (!item.slot9c_ok) return 0;

  // 外层计数 [item+0x2E8] <= 0 → 0x5A6C46 jle
  if (item.outer_count <= 0) return 0;
  // 内层计数 [item+0x2E4] <= 0 → 0x5A6C5E jle
  if (item.inner_count <= 0) return 0;

  const int W = ctx.w_bound;        // [0xABED04]
  const int W2H = ctx.w2h_bound;    // [0xABED08]

  // 双层循环：内层 edi → X、外层 ebx → Y（x 优先）
  for (int outer = 0; outer < item.outer_count; ++outer) {
    const int Y = static_cast<std::int16_t>(ctx.src_y + outer);
    for (int inner = 0; inner < item.inner_count; ++inner) {
      const int X = static_cast<std::int16_t>(ctx.src_x + inner);

      // 菱形四判定（有符号比较）
      const int sum = X + Y;
      if (sum <= W) continue;            // X+Y<=W → jle 跳过
      if (X - Y >= W) continue;          // X-Y>=W → jge 跳过
      if (Y - X >= W) continue;          // Y-X>=W → jge 跳过
      if (sum > W2H) continue;           // X+Y>W2H → jg 跳过

      void* cell = world.GetCellAt(X, Y);   // 0x5657A0

      // 索引串 = 外层×内层计数 + 内层（imul ecx,ebx）
      const int idx = outer * item.inner_count + inner;
      // 数据项 = 数据表[索引串*4 + 0x10]
      const bool have = idx >= 0 &&
          static_cast<std::size_t>(idx) < item.data.size() &&
          item.data[idx].present;
      if (!have) continue;               // 0x5A6CE2 je（GetCellAt 已调）

      const DeadCode6C10DataItem& di = item.data[idx];
      const std::uint8_t c11a = static_cast<std::uint8_t>(idx);
      std::uint8_t c11b = 0;
      if (ctx.layer != -1) {             // 0x5A6CF7 cmp -1 / je
        c11b = static_cast<std::uint8_t>(
            static_cast<int>(di.b28) + ctx.layer - 4);
      }
      const std::uint8_t c11c = di.b2a;
      world.WriteCell(cell, item.c294, c11a, c11b, c11c);

      // [0xABED10] 非 0 才写 work：(Y*side+X)*0x50 + 0x38 = 栈参1
      if (world.WorkPresent()) {
        world.WriteWork(X, Y, ctx.workv);
      }
    }
  }

  return 0;   // EAX 无意义
}

}  // namespace rmg
}  // namespace ra2