#include "crt_format.h"

namespace ra2::runtime {

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

}  // namespace ra2::runtime