#include "crt_format.h"

namespace ra2::crt {

std::int32_t vsprintfw_legacy(CrtFormatWorld& world, std::uint8_t* dst,
                              std::uint32_t fmt, std::uint32_t args) {
    std::uint8_t* slot = dst;
    std::int32_t limit = kVsprintfwLimit;

    const std::int32_t result = world.WideFormat(&slot, fmt, args);

    if (--limit < 0) {
        world.WriteTerminator(&slot, 0);
    } else {
        *slot = 0;
        ++slot;
    }

    if (--limit < 0) {
        world.WriteTerminator(&slot, 0);
    } else {
        *slot = 0;
    }

    return result;
}

// 0x7C8EF4：ANSI vsprintf 薄封装（单 NUL 变体）。外壳与 0x7CA564 同构，
// 但核心引擎是 0x7CE2A5 且只写一个 NUL、槽不推进。
std::int32_t vsprintf_legacy(CrtFormatWorld& world, std::uint8_t* dst,
                             std::uint32_t fmt, std::uint32_t args) {
    std::uint8_t* slot = dst;
    std::int32_t limit = kVsprintfwLimit;

    const std::int32_t result = world.WideFormat(&slot, fmt, args);

    if (--limit < 0) {
        world.WriteTerminator(&slot, 0);
    } else {
        *slot = 0;  // 只写一个 NUL，槽不推进（0x7C8F2F-0x7C8F32）
    }

    return result;
}

}  // namespace ra2::crt