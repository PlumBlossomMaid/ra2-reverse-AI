#pragma once

#include <cstdint>

namespace ra2::crt {

// 0x7CA5D3 MSVCRT wcscmp（宽字符串比较，cdecl 2 栈参，返回三态 -1/0/+1）。
// 机器码 53B（0x7CA5D3-0x7CA607）：
//   edx=[esp+4]=s1；push esi 后 esi=[esp+0xC]=s2；cx=[esi]
//   循环 0x7CA5E0：movzx eax,[edx]（*s1 零扩展）；movzx edi,cx；
//     sub eax,edi（32 位无符号减）；jne 退出；test cx,cx（*s2==0）；
//     je 退出；inc edx ×2 / inc esi ×2；mov cx,[esi]；jmp 循环
//   退出：test eax,eax；jge →（jle → ret 0；否则 push 1/pop eax → ret 1）
//     ；eax<0 → or eax,-1 → ret -1
// ★怪癖：①NUL 检测只看 s2（s1 的 NUL 由差值体现：s1 短且前缀相同 →
//   0-c2<0 → -1）；②差值 32 位无符号减法、符号由 test/jge 判
//   （字符域 0-0xFFFF 不回绕）；③返回三态归一化，非原始差值。
// 纯内存零子调用零全局（golden 全真身模拟，deadcode_7ca5d3_golden.txt）。
int wcscmp_legacy(const std::uint16_t* s1, const std::uint16_t* s2);

}  // namespace ra2::crt
