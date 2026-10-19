#include "crt_wcschr.h"

#include <cstdint>

namespace ra2::runtime {

const std::uint16_t* wcschr_legacy(const std::uint16_t* s, std::uint16_t c) {
    const std::uint16_t* p = s;
    for (;;) {
        const std::uint16_t ch = *p;
        if (ch == 0) break;   // test cx,cx / je 0x7CA8DD（串尾）
        if (ch == c) break;   // cmp cx,word[esp+8] / je 0x7CA8DD（命中）
        ++p;                  // inc eax ×2（宽字符步进）
    }
    // 收尾（0x7CA8DD）：cx = *p - c（16 位）→ neg（CF = cx!=0）
    //   → sbb ecx,ecx（-CF）→ not ecx（命中 -1 / 未命中 0）
    //   → and eax,ecx（命中保留指针 / 未命中清 0）
    const std::uint16_t d = static_cast<std::uint16_t>(*p - c);
    // 原版 x86 `and eax,ecx` 只 32 位；64 位宿主须全 1 掩码保留完整指针
    const std::uintptr_t mask =
        (d == 0) ? ~std::uintptr_t{0} : std::uintptr_t{0};
    return reinterpret_cast<const std::uint16_t*>(
        reinterpret_cast<std::uintptr_t>(p) & mask);
}

}  // namespace ra2::runtime