#include "crt_wcsncpy.h"

namespace ra2::crt {

std::uint16_t* wcsncpy_legacy(std::uint16_t* dst, const std::uint16_t* src,
                              std::size_t count) {
    // 复制循环（0x7CA436）：ax=*src → *dst=ax → 指针+2 →
    //   test ax,ax 遇 NUL 停（NUL 已写入）→ dec count 耗尽停
    std::uint16_t* d = dst;
    std::size_t n = count;
    for (; n > 0; --n) {
        const std::uint16_t ch = *src++;
        *d++ = ch;
        if (ch == 0) break;  // 0x7CA443 je 0x7CA448
    }
    // 填充路径（0x7CA448）：count 未耗尽时填 0
    //   n==0 → 无填充；n==1 → NUL 已写入、无填充；否则填 (n-1) 个 0
    if (n > 1) {
        std::uint16_t* p = d;
        for (std::size_t k = n - 1; k > 0; --k) *p++ = 0;
    }
    return dst;  // eax = esi（入口 dst）
}

}  // namespace ra2::crt