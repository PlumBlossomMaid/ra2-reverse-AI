// __alldiv（64 位有符号除法）逐字节复刻模型 —— batch 1027
//
// 0x7C92D0(170B) = MSVC6 CRT __alldiv——cdecl 4 dword 栈参 (a_lo, a_hi,
// b_lo, b_hi)，返回 edx:eax 64 位商 = (int64)a / (int64)b（有符号截断
// 向零，余数丢弃），ret 0x10 弹 16 字节。
//   ★动机：与 batch 975 已 REIMPL 的 __aulldiv（0x7C9380 无符号版）
//   相邻成对、仅差符号处理三段——同一模板两次实例化。
//
//   ★机器码全段（disasm 复核，batch 1027）：
//     push edi / push esi / push ebx
//     xor edi, edi                    ; edi = 符号计数
//     mov eax,[esp+0x14]=a_hi → or eax,eax → jge 0x7C92F1（★a_hi>=0 跳）
//       inc edi
//       mov edx,[esp+0x10]=a_lo → neg eax / neg edx / sbb eax,0
//         （★★64 位取负 a = -a：neg 低 32 清 CF、neg 高 32、sbb 借位）
//       mov [esp+0x14],eax / mov [esp+0x10],edx
//     0x7C92F1: mov eax,[esp+0x1c]=b_hi → or eax,eax → jge 0x7C930D
//       inc edi / mov edx,[esp+0x18]=b_lo → neg/neg/sbb（b = -b）
//       mov [esp+0x1c],eax / mov [esp+0x18],edx
//     0x7C930D: or eax,eax → jne 0x7C9329（★★b_hi!=0 → 硬路径）
//     快速路径（0x7C9311，b_hi==0）：
//       mov ecx,[esp+0x18]=b_lo
//       mov eax,[esp+0x14]=a_hi → xor edx,edx → div ecx
//         （★edx:eax = 0:a_hi / b_lo → eax=高商，edx=余）
//       mov ebx,eax → mov eax,[esp+0x10]=a_lo → div ecx
//         （★edx:eax = 余:a_lo / b_lo → eax=低商）
//       mov edx,ebx → jmp 0x7C936A（★edx:高商 + eax:低商）
//     硬路径（0x7C9329，b_hi!=0）：
//       mov ebx,eax（=b_hi）→ mov ecx,[esp+0x18]=b_lo
//       mov edx,[esp+0x14]=a_hi → mov eax,[esp+0x10]=a_lo
//       归一化循环（0x7C9337）：
//         shr ebx,1 / rcr ecx,1（★b_hi:b_lo 联合右移）
//         shr edx,1 / rcr eax,1（★a_hi:a_lo 联合右移）
//         or ebx,ebx → jne 循环（★直到 b_hi==0）
//       div ecx（★edx:eax = a 归一化 / b_lo' → eax=预商 esi）
//       mov esi,eax → mul dword[esp+0x1c]（★esi*b_hi → edx:eax）
//       mov ecx,eax → mov eax,[esp+0x18]=b_lo → mul esi
//         （★edx:eax = b_lo*esi）
//       add edx,ecx（★联合：商*b 的 128 位积高 64 = edx + 前积低32）
//       jb 0x7C9365（★CF=1 溢出 → 减商）
//       cmp edx,[esp+0x14]=a_hi → ja 0x7C9365 / jb 0x7C9366
//       cmp eax,[esp+0x10]=a_lo → jbe 0x7C9366（★积 ≤ a → 收尾）
//       0x7C9365: dec esi（★校正：商减 1）
//       0x7C9366: xor edx,edx → mov eax,esi（★edx=0，eax=商——上界同
//         __aulldiv：归一化后商 < 2^32，高 32 恒 0）
//     0x7C936A: dec edi → jne 0x7C9374（★★符号还原：edi 原值==1 才取负）
//       neg edx / neg eax / sbb edx,0（★64 位取负 商 = -商）
//     0x7C9374: pop ebx / pop esi / pop edi / ret 0x10
//
//   ★怪癖（与 __aulldiv 相同的模板，另有符号三段）：
//     ① 快速/硬路径分岔在 b_hi（取绝对值后）——与无符号版同构；
//     ② 快速路径两次 div 分步（先高商后低商，edx 余数接力）；
//     ③ 硬路径归一化循环联合右移（shr/rcr 配对——b 最高位被挪进
//        b_lo 高位，保证 b_lo' ≥ 2^31 使预商不溢出）+ 乘法校正
//        （预商至多偏大 1 → dec 一次）；
//     ④ ★符号三段：入口对 a、b 各自 64 位取负（neg/neg/sbb——
//        只处理负数：先判 a_hi/b_hi 符号位，jge 跳过取负）+ 收尾
//        dec edi / jne 判定——edi 累计负号数，**原值 == 1 才取负**
//        （一个负 → 商负；零个或两个负 → 商正）——奇偶符号约定；
//     ⑤ 返回 edx:eax 64 位商、余数丢弃；
//     ⑥ 除零（b==0）走 div #DE 异常——调用方契约保证非零
//        （golden 不含除零用例）；符号取负的边界 INT64_MIN = -a
//        自反（neg/sbb 惯用回绕）——照抄不额外处理。

#ifndef RA2_RUNTIME_CRT_ALLDIV_H_
#define RA2_RUNTIME_CRT_ALLDIV_H_

#include <cstdint>

namespace ra2::crt {

// __alldiv 逐字节复刻：有符号 64 位除法（截断向零）。除零未定义
// （原版 div #DE）。
std::int64_t alldiv_legacy(std::int64_t a, std::int64_t b);

}  // namespace ra2::crt

#endif  // RA2_RUNTIME_CRT_ALLDIV_H_