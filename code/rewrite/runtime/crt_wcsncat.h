#pragma once

#include <cstddef>
#include <cstdint>

namespace ra2::runtime {

// 0x7CB504 MSVCRT wcsncat（宽串追加定长，cdecl 3 栈参 (dst, src, Count)，
// 返回 dst——★eax 入口保存原始 dst 后全程不动即返回值）。机器码 64B
// （0x7CB504-0x7CB543）：
//   mov ecx,[esp+4]=dst / mov eax,ecx（★eax=原始 dst 备份=返回值）
//   扫 NUL 循环（0x7CB50A）：
//     mov dx,word[ecx]（★读 2 字节 u16）→ inc ecx ×2 → test dx,dx
//     → jne（★扫到 dst 尾 NUL，ecx 越过 NUL）
//   push ebx / mov ebx,[esp+0x10]=Count（★3 参 cdecl：dst/src/Count）
//   dec ecx ×2（★回退到 NUL 位置）
//   mov edx,ebx / dec ebx（★ebx=Count-1）
//   test edx,edx → je 0x7CB53D（★Count==0 → 直接 [ecx]=0 结尾）
//   复制循环（0x7CB527）：
//     mov dx,word[esi]（esi=src）→ mov word[ecx],dx（★写 dst 尾）
//     inc ecx ×2 / inc esi ×2（★双指针各 2B 步进）
//     test dx,dx → je 0x7CB541（★src 到 NUL → 自然终止，返回）
//     mov edx,ebx → dec ebx → test edx,edx → jne 循环（★Count 限制）
//   0x7CB53D: and word[ecx],0（★Count 耗尽 → 写 NUL 结尾）
//   0x7CB541: pop esi / pop ebx / ret（★eax=原始 dst 返回）
// ★怪癖：①eax 在入口保存原始 dst 后全程不动——返回 dst（标准
//   wcscat/wcsncat 语义）；②src NUL 时该 NUL 被复制进 dst（自然终
//   止——golden src 提前 NUL 实证）；③Count 耗尽时补写 NUL（即使
//   src 未完）；④Count==0 时仅写 NUL（[ecx]=0，原 dst 不变）；⑤
//   ebp 帧缺失（esp 直访——MSVC6 CRT 串函数惯用）；⑥wcs 2B 步进。
// 纯内存零子调用零全局（golden 全真身模拟，deadcode_7cb504_golden.txt，
// 10 用例）。★调用方合约：dst 须为已终止宽串 + 缓冲区足够（扫 NUL
// 前提 + 写 dst 尾 + Count 字符 + NUL）。
std::uint16_t* wcsncat_legacy(std::uint16_t* dst, const std::uint16_t* src,
                              std::size_t count);

}  // namespace ra2::runtime