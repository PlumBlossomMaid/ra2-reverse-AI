#pragma once

#include <cstdint>

namespace ra2::runtime {

// 0x7CA45F MSVCRT wcscat（宽字符串连接，cdecl 2 栈参，返回 dst）。
// 机器码 42B（0x7CA45F-0x7CA488）：
//   eax=[esp+4]=dst / ecx=dst
//   cmp word [eax],0 → je 0x7CA473（dst 空串快路径）
//   找 NUL 循环（0x7CA46B）：inc ecx ×2 / cmp word [ecx],0 / jne——
//     ecx 停在 dst 终止 NUL
//   复制循环（0x7CA478）：
//     si=*(u16*)edx（src）；*(u16*)ecx=si（写 dst）
//     inc ecx ×2 / inc edx ×2 / test si,si / jne——src 到 NUL 停
//   pop esi / ret（eax 恒为 dst 原值 → 返回 dst）
// ★怪癖：①找 NUL 用 16 位比较（宽字符）；②dst[0]==0 快路径跳过
//   扫描直接复制；③src 的 NUL 复制进 dst（标准 wcscat 语义：dst
//   终止 NUL 被覆盖、src 终止 NUL 收尾）；④无 dst 容量检查（调用
//   者保证）；⑤32 位指针步进用 inc ×2 而非 add 2。纯内存零子调用
//   零全局（golden 全真身模拟，deadcode_7ca45f_golden.txt）。
std::uint16_t* wcscat_legacy(std::uint16_t* dst, const std::uint16_t* src);

}  // namespace ra2::runtime