#include "crt_wcscpy.h"

namespace ra2::crt {

std::uint16_t* wcscpy_legacy(std::uint16_t* dst, const std::uint16_t* src) {
    std::uint16_t* out = dst;
    // 原版流水线：先读首字符写入，再在 ecx 推进后 test——
    // 等效 `c=src[0]; dst[0]=c; while(c){c=*src++; *dst++=c;}`
    std::uint16_t c = *src++;
    *dst++ = c;
    while (c != 0) {
        c = *src++;
        *dst++ = c;
    }
    return out;
}

}  // namespace ra2::crt