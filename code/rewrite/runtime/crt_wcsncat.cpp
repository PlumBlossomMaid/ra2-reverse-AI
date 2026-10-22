#include "crt_wcsncat.h"

#include <cstdint>

namespace ra2::crt {

std::uint16_t* wcsncat_legacy(std::uint16_t* dst, const std::uint16_t* src,
                              std::size_t count) {
    // ★eax 入口保存原始 dst（mov eax,ecx）——全程不动，ret 即返回值
    std::uint16_t* const ret = dst;

    // 扫 NUL 循环（0x7CB50A）：mov dx,word[ecx] / inc ecx ×2 / test
    // dx,dx / jne——扫到 dst 尾 NUL（ecx 越过 NUL）
    std::uint16_t* p = dst;
    while (*p != 0) ++p;
    // dec ecx ×2（0x7CB519/0x7CB51C）——回退到 NUL 位置

    // ebx=Count-1（0x7CB51D dec ebx）；test edx（Count）→ je（Count==0
    // → 0x7CB53D 直接 [ecx]=0 结尾——原 dst 不变）
    if (count == 0) {
        *p = 0;
        return ret;
    }

    // 复制循环（0x7CB527）：
    //   mov dx,word[esi] → mov word[ecx],dx（写 dst 尾）
    //   inc ecx ×2 / inc esi ×2（双指针各 2B 步进）
    //   test dx,dx → je 0x7CB541（src 到 NUL → 自然终止）
    //   mov edx,ebx → dec ebx → test edx,edx → jne 循环（Count 限制）
    const std::uint16_t* s = src;
    for (;;) {
        const std::uint16_t ch = *s;
        *p = ch;  // ★src 的 NUL 会被复制进 dst（自然终止）
        ++p;
        ++s;
        if (ch == 0) return ret;  // je 0x7CB541（src NUL 已复制）
        if (--count == 0) break;  // mov edx,ebx / dec ebx → 0 → 退出
    }
    *p = 0;  // 0x7CB53D: and word[ecx],0（Count 耗尽 → 补写 NUL）
    return ret;
}

}  // namespace ra2::crt