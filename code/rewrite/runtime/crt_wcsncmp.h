#pragma once

#include <cstddef>
#include <cstdint>

namespace ra2::runtime {

// 0x7CB4CC MSVCRT wcsncmp（宽串受限比较，cdecl 3 栈参 (s1, s2, Count)，
// 返回当前两字符之差——★零扩展无符号差值，非三态）。机器码 56B
// （0x7CB4CC-0x7CB503）：
//   push ebp / mov ebp,esp            ; ★老式 ebp 帧（MSVC6 CRT __cdecl）
//   cmp dword ptr [ebp+0x10],0        ; ★Count
//   jne 0x7CB4D9 → xor eax,eax / pop ebp / ret   ; ★Count==0 → 返 0
//   0x7CB4D9: dec dword ptr [ebp+0x10]   ; ★Count 先减 1
//   mov edx,[ebp+0xC]=s2 / mov ecx,[ebp+8]=s1
//   je 0x7CB4FA                       ; ★dec 后 Count==0 → 直奔收尾
//   循环 0x7CB4E4:
//     mov ax,word ptr [ecx]           ; ★s1 当前宽字符（2B 小端）
//     test ax,ax → je 0x7CB4FA        ; ★s1 遇 NUL → 收尾
//     cmp ax,word ptr [edx] → jne 0x7CB4FA  ; ★不等 → 收尾
//     inc ecx ×2 / inc edx ×2         ; ★双指针各 2B 步进（wchar_t）
//     dec dword ptr [ebp+0x10] → jne 循环    ; ★Count 限制
//   0x7CB4FA 收尾:
//     movzx eax,word ptr [ecx]        ; ★零扩展 s1 当前字符
//     movzx ecx,word ptr [edx]        ; ★零扩展 s2 当前字符
//     sub eax,ecx                     ; ★★返回字符差（s1-s2）
//   pop ebp / ret
// ★怪癖：①Count==0 直接返 0 不比较；②Count 先 dec 再循环（经典 CRT
//   少比一个写法——dec 后为 0 直奔收尾比较首字符）；③s1 遇 NUL 提前
//   退出但仍返差（0 - s2[当前]，负值）；④movzx 零扩展 u16 后相减——
//   返回原始差值非三态（如 'a'-'A'=32）；⑤老式 ebp 帧。
// 纯内存零子调用零全局（golden 全真身模拟，deadcode_7cb4cc_golden.txt，
// 18 用例）。
int wcsncmp_legacy(const std::uint16_t* s1, const std::uint16_t* s2,
                   std::size_t count);

}  // namespace ra2::runtime