// 死代码复现 —— 0x5A7640 点集 bounding box 计算器（batch 936）
//
// 语义链见 dead_code_7640.h 头注释。实现要点（全部有汇编依据 + emu 黄金
// 实证，golden 8 用例：deadcode_7640_golden.txt）：
//   · 变量：minX=di 初值 999(0x3E7)、minY=bx 初值 999、maxX=ax 初值 0、
//     maxY=bp 初值 0（16 位寄存器）
//   · 主循环（0x5A7658-0x5A76AC，从数组末尾往前遍历）：
//       读元素 dword（0x5A766D）：低 16 = X、高 16 = Y
//       minX = X < minX ? X : minX（0x5A766F cmp dx,di; jge 跳过）
//       minY = Y < minY ? Y : minY（0x5A767B-0x5A7685）
//       maxX = X > maxX ? X : maxX（0x5A7688 cmp dx,ax; jle 跳过）
//       maxY = Y > maxY ? Y : maxY（0x5A7690 cmp si,bp; jle 跳过）
//   · 输出段（0x5A76AE-0x5A76CB）：movsx 符号扩展 16→32 后写 out：
//       out[0] = (s32)minX；out[1] = (s32)minY；
//       out[2] = (s32)maxX - (s32)minX + 1；
//       out[3] = (s32)maxY - (s32)minY + 1
//
// 原版怪癖（golden 实证，照抄）：
//   ★① min 初值 999、max 初值 0 → 空数组（n==0）输出 {999,999,-998,-998}
//      哨兵（golden empty）
//   ★② 全负坐标时 max 保持初值 0（golden allneg：{ -2,-2, 3,3}）
//   ③ X/Y 各 16 位独立比较（有符号），元素布局 {short X, short Y} 4B
//   ④ 从数组末尾往前遍历，顺序不影响 min/max
//   ⑤ out[2]/out[3] = max-min+1（含端点）
#include "dead_code_7640.h"

#include <cstdint>

namespace ra2 {
namespace rmg {

void DeadCode7640(const DeadCode7640Context& ctx, std::int32_t out[4]) {
  // 16 位变量（0x5A7643 xor ax,ax / 0x5A764D mov di,0x3E7 /
  // 0x5A7651 xor bp,bp / 0x5A7655 mov bx,di）
  std::int16_t min_x = 999;   // di
  std::int16_t min_y = 999;   // bx
  std::int16_t max_x = 0;     // ax
  std::int16_t max_y = 0;     // bp

  // 主循环（0x5A7658 起；n==0 时 esi=n-1 为负 → js 直接跳输出段）
  for (std::int32_t i = ctx.count - 1; i >= 0; --i) {
    const std::uint32_t e = ctx.world->Element(i);
    const std::int16_t X = static_cast<std::int16_t>(e & 0xFFFF);
    const std::int16_t Y = static_cast<std::int16_t>(e >> 16);

    // 0x5A766F cmp dx,di; 0x5A7676 jge 跳过 → X < minX 才更新
    if (X < min_x) min_x = X;
    // 0x5A7680 cmp si,bx（si=高16 即 Y）→ Y < minY 才更新
    if (Y < min_y) min_y = Y;
    // 0x5A7688 cmp dx,ax; 0x5A768B jle 跳过 → X > maxX 才更新
    if (X > max_x) max_x = X;
    // 0x5A7690 cmp si,bp → Y > maxY 才更新
    if (Y > max_y) max_y = Y;
  }

  // 输出段（0x5A76AE-0x5A76CB，movsx 16→32 符号扩展）
  out[0] = min_x;
  out[1] = min_y;
  out[2] = static_cast<std::int32_t>(max_x) - static_cast<std::int32_t>(min_x) + 1;
  out[3] = static_cast<std::int32_t>(max_y) - static_cast<std::int32_t>(min_y) + 1;
}

}  // namespace rmg
}  // namespace ra2