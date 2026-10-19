#include "crt_wcslen.h"

namespace ra2::runtime {

std::uint32_t wcslen_legacy(const std::uint16_t* s) {
    if (s[0] == 0) return 0;  // 空串特例（原版独立分支）
    std::uint32_t k = 1;
    while (s[k] != 0) ++k;    // 从 s[1] 起找第一个 NUL
    return k;                 // NUL 索引 = 字符串长度
}

}  // namespace ra2::runtime