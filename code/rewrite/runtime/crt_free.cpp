#include "crt_free.h"

namespace ra2::runtime {

void free_legacy(CrtFreeWorld* world, void* p) {
    if (p == nullptr) return;  // 0x7C93ED test esi,esi / je 直接返回
    world->enter_lock(9);      // 0x7C93F1 push 9 / call 0x7CD9F5
    void* seg = world->find_segment(p);  // 0x7C93F8 call 0x7CF7BD
    if (seg != nullptr) {
        // 命中：FreeSegment(seg, p) + LeaveLock(9)
        world->free_segment(seg, p);     // 0x7C9406 call 0x7CF7E8
        world->leave_lock(9);            // 0x7C940D call 0x7CDA56
    } else {
        // 未命中：LeaveLock(9) + HeapFree(heap, 0, p)
        world->leave_lock(9);            // 0x7C9419 call 0x7CDA56
        world->heap_free(world->heap_handle, 0, p);  // 0x7C9428 call [0x7E127C]
    }
}

}  // namespace ra2::runtime