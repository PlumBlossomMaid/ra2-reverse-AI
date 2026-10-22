#pragma once

#include <cstdint>

namespace ra2::crt {

// 0x7C8EC0 MSVC6 CRT __allmul（64 位乘法，cdecl 4 dword 栈参
// (a_lo, a_hi, b_lo, b_hi)，返回 edx:eax 64 位乘积，ret 0x10）。
// 机器码 52B（0x7C8EC0-0x7C8EF1）：
//   mov eax,[esp+8]=a_hi
//   mov ecx,[esp+0x10]=b_hi
//   or ecx,eax                  ; ★标志 = a_hi|b_hi（mov 不改标志）
//   mov ecx,[esp+0xC]=b_lo      ; ecx 覆写为 b_lo
//   jne 0x7C8ED9                ; ★a_hi|b_hi != 0 → 硬路径
//   快速路径（0x7C8ED0）：
//     mov eax,[esp+4]=a_lo
//     mul ecx                   ; edx:eax = a_lo*b_lo（64 位）
//     ret 0x10
//   硬路径（0x7C8ED9）：
//     push ebx
//     mul ecx                   ; edx:eax = a_hi*b_lo（eax 仍=a_hi）
//     mov ebx,eax               ; ebx = (a_hi*b_lo) 低32
//     mov eax,[esp+8]           ; ★push 后 esp-4：[esp+8]=a_lo
//     mul dword ptr [esp+0x14]  ; ★[esp+0x14]=b_hi：edx:eax = a_lo*b_hi
//     add ebx,eax               ; ebx += (a_lo*b_hi) 低32（32 位回绕）
//     mov eax,[esp+8]           ; eax = a_lo
//     mul ecx                   ; edx:eax = a_lo*b_lo（64 位）
//     add edx,ebx               ; edx += ebx（★32 位回绕，溢出丢弃）
//     pop ebx
//     ret 0x10
// ★怪癖：①a_hi*b_hi 项完全丢弃（64 位结果溢出回绕——与标准 64 位
//   无符号乘法 mod 2^64 等价）；②两次 add 均 32 位回绕、进位不进
//   结果高 32；③快速/硬路径分岔只在 a_hi|b_hi（b_hi==0 且 a_hi==0
//   走快速）；④mul 前 edx 未初始化但 32 位 mul 总会写满 edx:eax。
// 纯算术零子调用零全局（golden 全真身模拟，deadcode_7c8ec0_golden.txt，
// 15 用例）。
uint64_t allmul_legacy(std::uint64_t a, std::uint64_t b);

}  // namespace ra2::crt