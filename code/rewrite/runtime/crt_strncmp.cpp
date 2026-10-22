#include "crt_strncmp.h"

#include <cstdint>

namespace ra2::crt {

int strncmp_legacy(const char* s1, const char* s2, std::size_t count) {
    if (count == 0) return 0;  // jecxz 0x7CA641
    // repne scasb：从 s1 扫 NUL（≤count），ecx 递减
    std::size_t n = 0;
    while (n < count && s1[n] != 0) ++n;
    // neg ecx / add ecx,ebx：比较长度 = min(count, len(s1)+1)
    const std::size_t clen = (n < count) ? n + 1 : count;
    // repe cmpsb：比较 clen 个字节，停在不等或耗尽
    std::size_t i = 0;
    while (i < clen) {
        const std::uint8_t a =
            static_cast<std::uint8_t>(s2[i]);
        const std::uint8_t b =
            static_cast<std::uint8_t>(s1[i]);
        if (a != b) {
            // 收尾：cmp al,[edi-1] / ja / je / dec×2 / not
            if (a > b) return -1;  // ja → not ecx(0) = -1
            if (a < b) return 1;   // dec ecx×2 → not(-2) = 1
        }
        ++i;
    }
    return 0;  // 耗尽相等 → eax=0
}

}  // namespace ra2::crt