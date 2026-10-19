#pragma once

#include <cstddef>
#include <cstdint>

namespace ra2::runtime {

// 0x7CA090（664B，74 unique_callers / 3211 transitive_dependents，
// ranking.tsv 扇入度最高的未复刻 CRT 项之一）：MSVC6 静态 CRT 重叠感知
// 字节拷贝 `memmove`（cdecl 3 栈参 ret、返回 dst）。零子调用零外部全局
// ——真身全程可模拟。
//
// 原版机器码（2026-10-19 disasm 全段复核 + golden 13 用例实证，
// memory/data/rmg/deadcode_7ca090_golden.txt）：
//
//   push ebp/mov ebp,esp/push edi/push esi
//   esi=[ebp+0xC]（src）；ecx=[ebp+0x10]（count）；edi=[ebp+8]（dst）
//   eax=src+count
//   cmp edi,esi / jbe 正向           ; dst <= src → 正向
//   cmp edi,eax / jb 反向(0x7CA228)   ; src < dst < src+count → 反向
// 正向路径（0x7CA0B0）：
//   test edi,3 / jne 0x7CA0CC        ; dst 未 4 对齐 → 先对齐字节
//   shr ecx,2 / and edx,3 / cmp ecx,8 / jb 跳转表
//   rep movsd / jmp [edx*4+0x7CA1D8] ; 对齐 dword 复制 + 尾部补齐
//   （非对齐：mov al,[esi]/mov [edi],al 逐字节补齐至对齐后同走）
// 反向路径（0x7CA228）：
//   lea esi,[src+count-4] / lea edi,[dst+count-4]  ; 尾部指针
//   test edi,3 / jne 0x7CA25C        ; dst 未 4 对齐 → 先对齐
//   shr ecx,2 / and edx,3 / cmp ecx,8 / jb 跳转表
//   std / rep movsd / cld / jmp [edx*4+0x7CA370]   ; 反向 dword + 补齐
//   （非对齐：反向逐字节对齐后同走）
// 收尾（各路径汇聚）：mov eax,[ebp+8]（返回 dst）/ pop esi/pop edi/
//   leave/ret
//
// ★原版怪癖（golden 实证，照抄）：
//   ① 返回 eax = dst（入口即存、永不修改）；
//   ② 重叠判定：dst<=src 或 dst>=src+count 走正向；src<dst<src+count
//      走反向（memmove 保序语义）；
//   ③ 正向/反向均按 dst 4 字节对齐优化（rep movsd + 尾部跳转表），
//      对齐优化不改变最终字节内容（行为等价）；
//   ④ 无溢出防护，纯逐字节/dword 复制。

// 0x7CA090：memmove。dst/src 为字节缓冲，count 为字节数（可为 0）。
// 返回 dst。重叠时保证保序（dst>src 且重叠走反向）。
void* memmove_090_legacy(void* dst, const void* src, std::size_t count);

}  // namespace ra2::runtime