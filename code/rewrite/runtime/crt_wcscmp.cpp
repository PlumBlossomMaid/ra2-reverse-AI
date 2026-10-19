#include "crt_wcscmp.h"

namespace ra2::runtime {

int wcscmp_legacy(const std::uint16_t* s1, const std::uint16_t* s2) {
    std::int32_t diff = 0;
    for (;;) {
        const std::uint32_t c1 = *s1;            // movzx eax, word [edx]
        const std::uint32_t c2 = *s2;            // cx（movzx edi, cx）
        diff = static_cast<std::int32_t>(c1 - c2);  // sub eax, edi（32 位无符号减）
        if (diff != 0) break;                    // jne 0x7CA5F8
        if (c2 == 0) break;                      // test cx,cx / je 0x7CA5F8（只看 s2 的 NUL）
        ++s1;                                    // inc edx ×2
        ++s2;                                    // inc esi ×2
    }
    // test eax,eax / jge / jle：-1 / 0 / +1 三态归一化
    if (diff < 0) return -1;
    if (diff > 0) return 1;
    return 0;
}

}  // namespace ra2::runtime
