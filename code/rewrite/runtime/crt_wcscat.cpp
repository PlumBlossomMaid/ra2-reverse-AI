#include "crt_wcscat.h"

namespace ra2::runtime {

std::uint16_t* wcscat_legacy(std::uint16_t* dst, const std::uint16_t* src) {
    std::uint16_t* d = dst;
    // 找 NUL（0x7CA465）：cmp word [eax],0 → je（空串快路径）；
    // 循环 inc ecx ×2 / cmp / jne——ecx 停在 dst 终止 NUL
    if (*d != 0) {
        do {
            ++d;
        } while (*d != 0);
    }
    // 复制循环（0x7CA478）：si=*src → *dst=si → 指针各+2 →
    //   test si,si（src 的 NUL 先写入再测停）
    const std::uint16_t* s = src;
    for (;;) {
        const std::uint16_t ch = *s++;
        *d++ = ch;
        if (ch == 0) break;  // test si,si / jne 0x7CA478
    }
    return dst;  // eax 恒为入口 dst
}

}  // namespace ra2::runtime