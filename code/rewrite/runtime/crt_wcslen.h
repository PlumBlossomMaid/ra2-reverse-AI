#pragma once

#include <cstdint>

namespace ra2::crt {

// 0x7CA405（29B，94 unique_callers / 3414 transitive_dependents，
// ranking.tsv 扇入度最高的未复刻 CRT 项之一）：MSVC6 静态 CRT 宽字符
// 字符串长度 `wcslen`（cdecl 1 栈参、ret 无栈清理）。
//
// ★batch 058 反编译歧义纠正：旧登记写 `while(s[i]==0)i++; return i-1`
// （Ghidra 反编译形式），逐条汇编复核实为 **找第一个 NUL 宽字符的索引
// 并返回它**（= 字符串长度）：
//   007ca405  8b4c2404   mov ecx, [esp+4]      ; ecx = s（第一参）
//   007ca409  66833900   cmp word [ecx], 0     ; s[0] == 0?
//   007ca40d  8d4102     lea eax, [ecx+2]      ; eax = s+2（字节）
//   007ca410  740a       je 0x7ca41c           ; s[0]==0 → 收尾（返回 0）
//   007ca412  668b10     mov dx, [eax]         ; dx = *(uint16_t*)eax
//   007ca415  40         inc eax
//   007ca416  40         inc eax               ; eax += 2
//   007ca417  6685d2     test dx, dx
//   007ca41a  75f6       jne 0x7ca412          ; dx != 0 → 循环
//   007ca41c  2bc1       sub eax, ecx          ; eax = 字节差
//   007ca41e  d1f8       sar eax, 1            ; /2（算术右移）
//   007ca420  48         dec eax               ; -1
//   007ca421  c3         ret
//
// 语义：s[0]==0 → 返回 0（空串特例）；否则从 s[1] 起逐字找第一个 NUL，
// 返回其索引（= 字符串长度）。实现用字节地址差 /2 再定位。
//
// ★原版怪癖（golden 实证，照抄）：
//   ① s[0]==0 走独立分支返回 0（空串特例）；
//   ② 非空串从 s[1] 起找 NUL，返回 NUL 索引 = 字符串长度
//      （sar /2 + dec，正数等价除法）；
//   ③ 无溢出防护，宽字符 16 位逐字扫描；
//   ④ 无任何外部全局/子调用——纯内存扫描，可整函数真身模拟。

// 0x7CA405：wcslen。s 为 16 位宽字符缓冲（NUL 结尾）。返回字符串长度。
std::uint32_t wcslen_legacy(const std::uint16_t* s);

}  // namespace ra2::crt