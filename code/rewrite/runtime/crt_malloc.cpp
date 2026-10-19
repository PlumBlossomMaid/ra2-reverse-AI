#include "crt_malloc.h"

namespace ra2::runtime {

void* malloc_legacy(CrtMallocWorld* world, std::size_t size) {
    // 0x7C9430：push [0xB782C4]（全局）→ push [esp+8]（size 转发）
    // → call 0x7C9442 → pop ecx×2 → ret
    // 参数顺序：core(size, 全局值)——push 全局先压、push size 后压，
    // 故 [esp+4]=size（最后 push）、[esp+8]=全局值。返回 eax 透传。
    return world->core(size, world->flag);
}

}  // namespace ra2::runtime