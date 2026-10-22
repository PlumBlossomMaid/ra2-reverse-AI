#pragma once

#include <cstdint>

namespace ra2::crt {

// 0x7C9380 MSVC6 CRT __aulldiv（64 位无符号除法，cdecl 4 dword 栈参
// (a_lo, a_hi, b_lo, b_hi)，返回 edx:eax 64 位商 = (uint64)a/(uint64)b，
// 无符号截断、余数丢弃，ret 0x10 弹 16 字节）。机器码 104B
// （0x7C9380-0x7C93E5）：
//   push ebx / push esi
//   mov eax,[esp+0x18]=b_hi → or eax,eax → jne 0x7C93A2（★b_hi!=0 →
//     硬路径）
//   快速路径（0x7C938A，b_hi==0）：
//     mov ecx,[esp+0x14]=b_lo
//     mov eax,[esp+0x10]=a_hi → xor edx,edx → div ecx
//       （★edx:eax = 0:a_hi / b_lo → eax=高商，edx=余）
//     mov ebx,eax → mov eax,[esp+0xC]=a_lo → div ecx
//       （★edx:eax = 余:a_lo / b_lo → eax=低商）
//     mov edx,ebx → jmp 0x7C93E3（★edx:高商 + eax:低商）
//   硬路径（0x7C93A2，b_hi!=0）：
//     mov ecx,eax（=b_hi）→ mov ebx,[esp+0x14]=b_lo
//     mov edx,[esp+0x10]=a_hi → mov eax,[esp+0xC]=a_lo
//     归一化循环（0x7C93B0）：
//       shr ecx,1 / rcr ebx,1（★b_hi:b_lo 联合右移，CF 传位）
//       shr edx,1 / rcr eax,1（★a_hi:a_lo 联合右移）
//       or ecx,ecx → jne 循环（★直到 b_hi==0，b 归一化到 32 位）
//     div ebx（★edx:eax = a 归一化 / b_lo' → eax=预商 esi）
//     mov esi,eax → mul dword [esp+0x18]（★esi*b_hi → edx:eax）
//     mov ecx,eax（★H 低 32——★H 高 32 被丢弃，原版不检查！）
//     mov eax,[esp+0x14]=b_lo → mul esi（★edx:eax = b_lo*esi）
//     add edx,ecx（★联合：积低 64 的高 32 = L_hi + H_lo，CF=进位）
//     jb 0x7C93DE（★CF=1 → 减商）
//     cmp edx,[esp+0x10]=a_hi → ja 0x7C93DE（★积高32 > a_hi → 减商）
//     jb 0x7C93DF → cmp eax,[esp+0xC]=a_lo → jbe 0x7C93DF（★积≤a → 收尾）
//     0x7C93DE: dec esi（★校正：商减 1）
//     0x7C93DF: xor edx,edx → mov eax,esi（★edx=0，eax=商）
//   0x7C93E3: pop esi / pop ebx / ret 0x10
// ★怪癖：①快速/硬路径分岔在 b_hi；②快速路径两次 div 分步（先高商后
//   低商，edx 余数接力——余*2^32|a_lo 除以 b_lo 永不溢出，因余<b_lo）；
//   ③硬路径归一化循环联合右移（shr/rcr 配对——b 最高位被挪进 b_lo
//   高位，保证 b_lo' ≥ 2^31 使预商不溢出）；④乘法校正只检查 CF（L_hi+
//   H_lo 进位）+ 积低 64 vs a——**H 高 32（esi*b_hi 溢出）被丢弃不检查**
//   （原版怪癖，归一化保证预商至多偏大 1，CF 或 64 位比较足以校正）；
//   ⑤返回 edx:eax 64 位商、余数丢弃；⑥除零（b==0）走 div #DE 异常——
//   调用方契约保证非零（golden 不含除零用例）。
// 纯算术零子调用零全局（golden 全真身模拟，deadcode_7c9380_golden.txt，
// 16 用例）。
std::uint64_t aulldiv_legacy(std::uint64_t a, std::uint64_t b);

}  // namespace ra2::crt