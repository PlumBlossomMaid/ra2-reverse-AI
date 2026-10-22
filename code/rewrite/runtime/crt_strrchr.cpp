#include "crt_strrchr.h"

#include <cstddef>
#include <cstdint>

namespace ra2::crt {

const char* strrchr_legacy(const char* s, int c) {
    // 正扫 NUL（xor eax,eax/or ecx,-1/repne scasb；inc ecx/neg ecx）：
    // ECX = len+1（含 NUL 的字节数——SCASB 相等命中时也先执行后递减）
    std::size_t len = 0;
    while (s[len] != 0) ++len;

    // 反向扫描（dec edi 指向 NUL 处 → std → repne scasb）：覆盖
    // [len..0] 共 len+1 个位置（含 NUL 与 s[0]）——★与 strchr 相反，
    // c 只取低 8 位（mov al,[ebp+0xC]）
    const std::uint8_t target = static_cast<std::uint8_t>(c);
    std::ptrdiff_t i = static_cast<std::ptrdiff_t>(len);
    for (;;) {
        if (static_cast<std::uint8_t>(s[i]) == target) return s + i;
        if (i == 0) return nullptr;  // xor eax,eax（未命中返 0）
        --i;
    }
}

}  // namespace ra2::crt