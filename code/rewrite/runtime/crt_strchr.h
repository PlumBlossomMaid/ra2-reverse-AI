#pragma once

#include <cstddef>

namespace ra2::runtime {

// 0x7CAF30 MSVCRT strchr（字节串查找，cdecl 2 栈参，返回首次命中指针）。
// 机器码（0x7CAF30-0x7CAFEB，含 0x7EFEFEFF magic 扫描段）：
//   al=ch（零扩展）；edx=s
//   对齐探测（0x7CAF40）：test edx,3；未对齐逐字节 cmp cl,bl
//     （cl=[edx]/inc edx）；test cl,cl（NUL → 返 0）直到对齐
//   ebx 展开 4 字节 pattern（shl eax,8 / or ebx,eax / shl ebx,0x10 / or）
//   主循环（0x7CAF66 四字节批量扫描）：
//     ecx=*(u32*)edx；eax=ecx；esi=ecx+0x7EFEFEFF（NUL 检测 A）
//     edi=(ecx^pattern)+0x7EFEFEFF（命中检测）
//     ecx^=pattern；ecx^=-1 → ecx^=edi → and ecx,0x81010100
//     eax^=-1 → eax^=esi → and eax,0x81010100；edx+=4
//     ecx（命中）非 0 → 0x7CAFA8 逐字节回验；
//     eax（NUL）非 0 → 0x7CAF93 平假阳性后返 0 / 继续
//   回验（0x7CAFA8）：*(u32*)(edx-4) 逐字节 cmp bl（命中返
//     edx-4/-3/-2/-1，即指针位置）；遇 NUL（al/ah==0）返 0
// ★怪癖：①ch 零扩展取低 8 位（al），与 cl/bl 8 位比较；②ch==0 时
//   语义 = 找终止 NUL（golden 用例：s="abc" ch=0 → 返 &s[3]、空串
//   ch=0 → 返 &s[0]）；③未对齐路径 NUL 检测与命中同为逐字节；
//   ④四字节扫描的 0x81010100 位掩码 + 0x7EFEFEFF 加法 magic 检测
//   （假阳性经 0x7CAF93 二次过滤后继续扫描）。纯内存零子调用零全局。
const char* strchr_legacy(const char* s, int ch);

}  // namespace ra2::runtime