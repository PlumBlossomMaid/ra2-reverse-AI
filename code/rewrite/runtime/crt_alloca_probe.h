#pragma once

#include <cstdint>
#include <vector>

namespace ra2::crt {

// 0x7CA650 MSVCRT _alloca_probe（栈探测分配，编译器生成 _alloca 时 call）。
// 机器码 47B（0x7CA650-0x7CA67E）：
//   push ecx；cmp eax,0x1000；lea ecx,[esp+8]（基点 = 调用者 esp）
//   jb 收尾；循环：sub ecx,0x1000 / sub eax,0x1000 / test [ecx],eax；
//     cmp eax,0x1000 / jae 循环
//   收尾：sub ecx,eax（残量）→ test [ecx],eax；mov eax,esp；mov esp,ecx；
//     mov ecx,[eax]（恢复调用者 ecx）；mov eax,[eax+4]（返回地址）；
//     push eax；ret
// ★怪癖：①探测序列 = [base-0x1000*i, i=1..k] + [base-size]
//   （k = size/0x1000 向下取整；size 恰为页倍数时末次循环与收尾同址
//   重复探测）；②eax 语义 = 分配的栈字节数（无栈参数），返回后
//   esp = 调用者 esp - size；③rec 探测是 test 读（页提交副作用），
//   C++ 模型省略真实读、仅记录地址序列（golden 实证）。
// 纯内存零子调用零全局（golden 全真身模拟，deadcode_7ca650_golden.txt）。
std::uint32_t alloca_probe_legacy(std::uint32_t caller_esp,
                                  std::uint32_t size,
                                  std::vector<std::uint32_t>* probe_out);

}  // namespace ra2::crt