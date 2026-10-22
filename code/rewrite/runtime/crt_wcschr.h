#pragma once

#include <cstdint>

namespace ra2::crt {

// 0x7CA8C6 MSVCRT wcschr（宽字符查找，cdecl 2 栈参，返回命中指针）。
// 机器码 41B（0x7CA8C6-0x7CA8EE）：
//   mov eax,[esp+4]=s（串指针）
//   扫描循环（0x7CA8CA）：
//     cx=word[eax]（*s）→ test cx,cx → je 0x7CA8DD（★串到 NUL → 收尾）
//     cmp cx,word[esp+8]（★与目标宽字符 c 比较）→ je 0x7CA8DD（命中）
//     inc eax ×2（宽字符步进 +2）→ jmp 循环
//   收尾（0x7CA8DD）：
//     cx=word[eax] → sub cx,word[esp+8]（★cx = *位置 - c，16 位）
//     neg cx（★CF = (cx != 0)）→ sbb ecx,ecx（ecx = -CF）
//     → not ecx（★命中 CF=0 → ecx=-1；未命中 CF=1 → ecx=0）
//     → and eax,ecx（★命中保留指针 / 未命中清 0）
//   ret
// ★怪癖：①逐 2 字节扫描（宽字符），步进 inc ×2；②串到 NUL 即停，
//   NUL 不参与与 c 的匹配（c==0 时例外：命中终止 NUL）；③★c==0 时
//   返回指向串尾 NUL 的指针（标准 wcschr 语义）；④未命中返回 0；
//   ⑤收尾用 sub/neg/sbb/not/and 归一化（CF 标志驱动掩码）。
// 纯内存零子调用零全局（golden 全真身模拟，deadcode_7ca8c6_golden.txt）。
const std::uint16_t* wcschr_legacy(const std::uint16_t* s, std::uint16_t c);

}  // namespace ra2::crt