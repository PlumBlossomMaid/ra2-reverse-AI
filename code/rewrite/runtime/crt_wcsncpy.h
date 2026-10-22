#pragma once

#include <cstddef>
#include <cstdint>

namespace ra2::crt {

// 0x7CA422（61B，ranking.tsv uc=28 / transitive_dependents=933，
// 按 unique_callers 扇入度取的下一个未复刻 CRT 项）：MSVCRT wcsncpy
// 变体——cdecl 3 栈参 dst/src/count、ret 由调用者清栈、返回 dst。
// 纯内存操作零子调用零全局——全程可真身模拟。
//
// 原版机器码（2026-10-19 disasm 全段复核 + golden 10 用例实证，
// memory/data/rmg/deadcode_7ca422_golden.txt）：
//
//   ecx = count（第3参）；push esi/edi 后 edi = dst（第1参）、
//   edx = src（第2参）；esi = dst 保留返回
// 复制循环 0x7CA436：
//   ax = *(u16*)src → *(u16*)dst = ax → 指针各+2
//   test ax,ax → je 0x7CA448（NUL 已写入，停）
//   dec ecx → jne 循环（count 耗尽即停）
// 填充路径 0x7CA448（遇 NUL 或 count 未耗尽时进入）：
//   test ecx,ecx → je 返回（ecx==0：count 用完、无填充）
//   dec ecx → je 返回（原 ecx==1：NUL 是已被复制的最后字符、无填充）
//   xor eax；shr ecx,1 → rep stosd（填 (ecx-1)/2 个 dword 0）
//   adc ecx,ecx → rep stosw（填剩余 word 0）
//   —— 填充量 = (原 ecx-1) 个宽字符（NUL 已占 1 个位置）
// 收尾：eax = esi（原 dst）/ pop edi / pop esi / ret
//
// ★原版怪癖（golden 实证，照抄）：
//   ① NUL 先写入再测试（ax==0 时该位置已被复制）；
//   ② count==0 → 不复制不填充直接返回；
//   ③ 遇 NUL 停止后填充 (剩余-1) 个 0（NUL 已占一位）；
//   ④ 返回 eax = 入口 dst。

// 0x7CA422：wcsncpy。dst/src 为宽字符缓冲，count 为宽字符数上限。
std::uint16_t* wcsncpy_legacy(std::uint16_t* dst, const std::uint16_t* src,
                              std::size_t count);

}  // namespace ra2::crt