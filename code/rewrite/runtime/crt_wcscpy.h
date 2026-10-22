#pragma once

#include <cstdint>

namespace ra2::crt {

// 0x7CA489（37B，112 unique_callers / 3837 transitive_dependents，
// ranking.tsv 扇入度最高的未复刻 CRT 项之一）：MSVC6 静态 CRT 宽字符
// 字符串拷贝 `wcscpy`（cdecl 2 栈参、ret 无栈清理、返回 dst）。
//
// 原版机器码（2026-10-19 disasm 全段复核 + golden 6 用例实证，
// memory/data/rmg/deadcode_7ca489_golden.txt）：
//
//   007ca489  8b4c2408   mov ecx, [esp+8]   ; ecx = src（第二参）
//   007ca48d  8b442404   mov eax, [esp+4]   ; eax = dst（第一参）
//   007ca491  56         push esi
//   007ca492  668b11     mov dx, [ecx]      ; dx = src[0]
//   007ca495  8d7002     lea esi, [eax+2]   ; esi = dst+2（字节偏移）
//   007ca498  668910     mov [eax], dx      ; dst[0] = dx
//   007ca49b  41         inc ecx            ; ecx → src[1]
//   007ca49c  41         inc ecx
//   007ca49d  6685d2     test dx, dx
//   007ca4a0  740a       je 0x7ca4ac        ; src[0]==0 → 返回
//   007ca4a2  668b11     mov dx, [ecx]      ; dx = 当前源字符
//   007ca4a5  668916     mov [esi], dx      ; 写目标
//   007ca4a8  46         inc esi            ; esi += 2
//   007ca4a9  46         inc esi
//   007ca4aa  ebef       jmp 0x7ca49b       ; 回 inc ecx×2 + test
//   007ca4ac  5e         pop esi
//   007ca4ad  c3         ret                ; 返回 eax = dst
//
// 语义：逐 16 位宽字符复制直到 NUL（含 NUL 本身），返回 dst。
// ★原版怪癖（golden 实证，照抄）：
//   ① 返回 eax = dst（入口即存，永不修改）；
//   ② "读-写-推进-测试"流水线：dx 载入后在 ecx 推进之后才 test——
//      等效 `c=src[0]; dst[0]=c; while(c){c=*src++; *dst++=c;}`，
//      NUL 也会被写入 dst 后才返回；
//   ③ 纯 cdecl 2 参、无溢出防护、宽字符 16 位逐字复制；
//   ④ 无任何外部全局/子调用——纯内存复制，可整函数真身模拟。

// 0x7CA489：wcscpy。src/dst 为 16 位宽字符缓冲（NUL 结尾）。返回 dst。
std::uint16_t* wcscpy_legacy(std::uint16_t* dst, const std::uint16_t* src);

}  // namespace ra2::crt