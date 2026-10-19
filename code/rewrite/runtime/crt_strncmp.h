#pragma once

#include <cstddef>

namespace ra2::runtime {

// 0x7CA610 MSVCRT _strncmp（定长字节串比较，cdecl 3 栈参，三态返回）。
// 机器码 56B（0x7CA610-0x7CA647）：
//   push ebp/mov ebp,esp/push edi/push esi/push ebx
//   ecx=[ebp+0x10]=count → jecxz 0x7CA641（count==0 → 返回 0）
//   ebx=count / edi=[ebp+8]=s1 / esi=edi / xor eax,eax
//   repne scasb（0x7CA624）：从 s1 扫 NUL，每字节 ecx--，
//     找到 NUL 或 ecx 耗尽停
//   neg ecx → add ecx,ebx → ecx = min(count, len(s1)+1)（★比较长度
//     = s1 到首个 NUL 的长度+1，若在 count 内；否则 = count）
//   mov edi,esi（s1 复位）/ esi=[ebp+0xC]=s2
//   repe cmpsb（0x7CA62F）：比较 ecx 个字节，停在不等或耗尽
//   收尾（0x7CA631）：al=[esi-1]（s2 最后字节）/ xor ecx,ecx /
//     cmp al,[edi-1]（s1 最后字节）
//     ja 0x7CA63F → not ecx(0) → eax=-1（s2>s1 → s1<s2 → -1）
//     je 0x7CA641 → eax=0（相等）
//     dec ecx ×2 → not ecx(-2) → eax=1（s2<s1 → s1>s2 → +1）
// ★怪癖：①count==0 直接返回 0（jecxz）；②比较长度 = min(count,
//   len(s1)+1)——先用 scasb 扫 s1 的 NUL（≤count），s2 不预扫（s1
//   短时 s1 的 NUL 提前终止比较，NUL=0 参与无符号判序）；③收尾
//   只取最后一对字节判三态，非原始差值；④ja/je 无符号比较；
//   ⑤返回 eax = -1/0/+1 三态。纯内存零子调用零全局（golden 全
//   真身模拟，deadcode_7ca610_golden.txt）。
int strncmp_legacy(const char* s1, const char* s2, std::size_t count);

}  // namespace ra2::runtime