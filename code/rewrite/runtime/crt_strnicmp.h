#pragma once

#include <cstddef>

namespace ra2::runtime {

// 0x7CD680 MSVCRT _strnicmp（不区分大小写定长比较，cdecl 3 栈参，三态
// 返回）。机器码 257B（0x7CD680-0x7CD77E）：
//   push ebp/push edi/push esi/push ebx
//   ecx=[ebp+0x10]=count → or ecx,ecx → je 0x7CD77A（count==0 → 返回 0）
//   esi=[ebp+8]=s1 / edi=[ebp+0xC]=s2
//   lea eax,[0xB78298] / cmp [eax+8],0 → jne 0x7CD6F1（★慢路径 locale）
//   快路径（0x7CD6A3，C locale [0xB782A0]==0）：
//     bh=0x41('A') / bl=0x5A('Z') / dh=0x20
//     循环（0x7CD6AC）：
//       ah=[esi] → or ah,ah → mov al,[edi] → je 收尾（ah==0）
//       → or al,al → je 收尾（al==0）
//       inc esi / inc edi
//       cmp ah,bh / jb → cmp ah,bl / ja → add ah,dh
//         （★仅 [0x41,0x5A] 内字符 +0x20 转小写）
//       cmp al,bh / jb → cmp al,bl / ja → add al,dh（s2 同）
//       cmp ah,al → jne 0x7CD6DF（ecx=-1 → jb 返回 -1 / neg 返回 1）
//       dec ecx → jne 循环（count 限制）
//     收尾（0x7CD6D5，任一 NUL 或 count 耗尽）：
//       xor ecx,ecx → cmp ah,al → je 0x7CD77A（相等返回 0）
//       → mov ecx,-1 → jb 0x7CD77A（★ah<al → s1<s2 → -1）
//       → neg ecx → +1（s1>s2）
//   0x7CD77A: mov eax,ecx / pop 族 / leave / ret
// ★怪癖：①count==0 直接返回 0；②任一字符串到 NUL 即提前终止（不继续
//   到 count 超限）；③仅 'A'-'Z'（0x41-0x5A）内字符 +0x20 转小写参与
//   比较（非 ASCII 字母原样、NUL 也原样但触发终止）；④返回三态
//   {-1,0,+1} 非原始差值；⑤★慢路径（[0xB782A0]!=0）依赖锁家族
//   0x7CD9F5 + tolower 内核 0x7CB063（batch 958 归档的镜像）——本
//   实现只复刻 C locale 快路径（[0xB782A0]==0，游戏默认），慢路径
//   留桩边界（见 frontier_batch_962.txt 三态判定）。
// 快路径纯内存零子调用零全局（golden 全真身模拟，
// deadcode_7cd680_golden.txt，21 用例）。
int strnicmp_legacy(const char* s1, const char* s2, std::size_t count);

}  // namespace ra2::runtime