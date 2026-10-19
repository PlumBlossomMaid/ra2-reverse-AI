#include "crt_memmove.h"

#include <cstdint>

namespace ra2::runtime {

void* memmove_090_legacy(void* dst, const void* src, std::size_t count) {
    std::uint8_t* d = static_cast<std::uint8_t*>(dst);
    const std::uint8_t* s = static_cast<const std::uint8_t*>(src);
    // 原版重叠判定：dst<=src 或 dst>=src+count → 正向；
    // src<dst<src+count → 反向（memmove 保序）。
    if (d <= s || d >= s + count) {
        for (std::size_t i = 0; i < count; ++i) d[i] = s[i];
    } else {
        for (std::size_t i = count; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return dst;
}

}  // namespace ra2::runtime