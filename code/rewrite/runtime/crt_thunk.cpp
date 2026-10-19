#include "crt_thunk.h"

namespace ra2::runtime {

std::uint32_t parse_thunk_legacy(CrtThunkWorld* world, std::uint32_t arg) {
    // 0x7C9BFD：push [esp+4]（arg 原样转发）→ call 0x7C9B72
    // → pop ecx（清转发参）→ ret。返回 eax 透传。
    return world->target(arg);
}

}  // namespace ra2::runtime